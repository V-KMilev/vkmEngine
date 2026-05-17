#include "gl_view.h"

#include <algorithm>
#include <vector>

#include "logger.h"

#include "config/gl_texture_mapping.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_texture.h"
#include "resource/shader_asset.h"
#include "resource/texture_asset.h"

namespace Engine {

namespace {

template<typename HandleT>
void sortUnique(std::vector<HandleT>& handles) {
    std::sort(handles.begin(), handles.end(),
        [](const HandleT& a, const HandleT& b) { return a.id() < b.id(); });
    handles.erase(std::unique(handles.begin(), handles.end(),
        [](const HandleT& a, const HandleT& b) { return a.id() == b.id(); }), handles.end());
}

void collectMeshHandles(const RenderView& view, std::vector<MeshHandle>& out) {
    out.clear();
    out.reserve(view.drawables.size());
    for (const auto& d : view.drawables) {
        if (d.mesh) out.push_back(d.mesh);
    }
    sortUnique(out);
}

void collectMaterialHandles(const RenderView& view, std::vector<MaterialHandle>& out) {
    out.clear();
    uint32_t lastId = UINT32_MAX;
    for (const auto& d : view.drawables) {
        if (!d.material) continue;
        if (d.material.id() == lastId) continue;  // sorted: skip consecutive duplicates
        lastId = d.material.id();
        out.push_back(d.material);
    }
    sortUnique(out);
}

void collectTextureHandles(
    const std::vector<MaterialHandle>& materialHandles,
    const ResourceManager& resources,
    std::vector<TextureHandle>& out
) {
    out.clear();
    for (const auto& mh : materialHandles) {
        const auto& material = resources.get(mh);
        for (const auto& mapping : g_textureMappings) {
            const TextureHandle& th = material.*mapping.handlePtr;
            if (th) out.push_back(th);
        }
    }
    sortUnique(out);
}

} // namespace

GLView::~GLView() {
    LOG_TRACE("Destructed GLView");
}

template<typename AssetT, typename GLT>
void GLView::syncTable(
    GLResourceTable<GLT>& table,
    const std::vector<Handle<AssetT>>& handles,
    const ResourceManager& resources
) {
    for (const auto& h : handles) {
        const uint32_t id = h.id();
        const auto& asset = resources.get(h);
        const uint64_t v = asset.version;

        if (id >= table.entries.size()) {
            table.entries.resize(id + 1);
            table.versions.resize(id + 1, 0);
        }

        if (!table.entries[id]) {
            table.entries[id] = std::make_unique<GLT>(asset);
            table.versions[id] = v;
        } else if (table.versions[id] != v) {
            table.entries[id]->update(asset);
            table.versions[id] = v;
        }
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    // Drawable-driven sync (meshes/materials/textures): early-out when neither
    // resource versions nor drawable count have changed.
    const uint64_t meshTypeVersion     = resources.getTypeVersion<MeshAsset>();
    const uint64_t materialTypeVersion = resources.getTypeVersion<MaterialAsset>();
    const uint64_t textureTypeVersion  = resources.getTypeVersion<TextureAsset>();
    const size_t drawableCount = view.drawables.size();

    const bool resourcesDirty =
           meshTypeVersion     != m_lastMeshTypeVersion
        || materialTypeVersion != m_lastMaterialTypeVersion
        || textureTypeVersion  != m_lastTextureTypeVersion
        || drawableCount       != m_lastDrawableCount;

    thread_local std::vector<MeshHandle>     meshHandles;
    thread_local std::vector<MaterialHandle> materialHandles;
    thread_local std::vector<TextureHandle>  textureHandles;

    if (resourcesDirty) {
        collectMeshHandles(view, meshHandles);
        collectMaterialHandles(view, materialHandles);
        collectTextureHandles(materialHandles, resources, textureHandles);

        syncTable<MeshAsset>    (m_meshTable,     meshHandles,     resources);
        syncTable<MaterialAsset>(m_materialTable, materialHandles, resources);
        syncTable<TextureAsset> (m_textureTable,  textureHandles,  resources);

        m_lastMeshTypeVersion     = meshTypeVersion;
        m_lastMaterialTypeVersion = materialTypeVersion;
        m_lastTextureTypeVersion  = textureTypeVersion;
        m_lastDrawableCount       = drawableCount;
    }

    // Per-frame UBOs owned by GLView. Shadow UBO is bound by GLShadowPass
    // after it populates entries — no need to bind here.
    m_camera.update(view.camera, view.environment);
    m_camera.bind();

    m_lights.update(view.lights);
    m_lights.bind();

    // Build instance batches once so all subsequent passes (shadow + forward)
    // share the same batched draw list.
    m_instanceBatcher.build(view.drawables);
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_meshTable.entries.size() || !m_meshTable.entries[id]) {
        LOG_WARNING("GLMesh not found for handle %u (not synced or invalid)", id);
        return nullptr;
    }
    return m_meshTable.entries[id].get();
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_materialTable.entries.size() || !m_materialTable.entries[id]) {
        LOG_WARNING("GLMaterial not found for handle %u (not synced or invalid)", id);
        return nullptr;
    }
    return m_materialTable.entries[id].get();
}

const GLTexture* GLView::getTexture(const TextureHandle& handle) const {
    const uint32_t id = handle.id();
    if (id >= m_textureTable.entries.size() || !m_textureTable.entries[id]) {
        LOG_WARNING("GLTexture not found for handle %u (not synced or invalid)", id);
        return nullptr;
    }
    return m_textureTable.entries[id].get();
}

GLMesh* GLView::getMutableMesh(const MeshHandle& handle) {
    const uint32_t id = handle.id();
    if (id >= m_meshTable.entries.size()) return nullptr;
    return m_meshTable.entries[id].get();
}

// Shaders aren't referenced by entities, so we resolve them lazily — one
// call per pass per frame. syncTable handles the "build on first reference
// or rebuild on version bump" path; hot reload threads through here.
GLShader* GLView::resolveShader(const ShaderHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    std::vector<ShaderHandle> one{handle};
    syncTable<ShaderAsset>(m_shaderTable, one, resources);
    return m_shaderTable.entries[handle.id()].get();
}

const GLMaterial* GLView::ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    std::vector<MaterialHandle> one{handle};
    syncTable<MaterialAsset>(m_materialTable, one, resources);
    return m_materialTable.entries[handle.id()].get();
}

GLMesh* GLView::ensureMesh(const MeshHandle& handle, const ResourceManager& resources) {
    if (!handle) return nullptr;
    std::vector<MeshHandle> one{handle};
    syncTable<MeshAsset>(m_meshTable, one, resources);
    return m_meshTable.entries[handle.id()].get();
}

void GLView::ensureMaterialTextures(const MaterialHandle& handle,
                                    const ResourceManager& resources) {
    if (!handle) return;
    const auto& material = resources.get(handle);

    thread_local std::vector<TextureHandle> texs;
    texs.clear();
    for (const auto& mapping : g_textureMappings) {
        const TextureHandle& th = material.*mapping.handlePtr;
        if (th) texs.push_back(th);
    }
    if (!texs.empty()) {
        sortUnique(texs);
        syncTable<TextureAsset>(m_textureTable, texs, resources);
    }
}

} // namespace Engine
