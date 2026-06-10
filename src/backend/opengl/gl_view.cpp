#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_view.h"

#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "data/gl_mesh.h"
#include "data/gl_material.h"
#include "data/gl_texture.h"

namespace Engine {

template <typename GLT, typename AssetT>
void GLView::ensure(GLResourceTable<GLT>& table, const Handle<AssetT>& handle, const ResourceManager& resources) {
    if (!handle) return;

    const uint32_t id = handle.id();
    const AssetT& asset = resources.get(handle);

    if (id >= table.entries.size()) {
        table.entries.resize(id + 1);
        table.versions.resize(id + 1, 0);
    }

    if (!table.entries[id]) {
        table.entries[id] = std::make_unique<GLT>(asset);
        table.versions[id] = asset.version;
    } else if (table.versions[id] != asset.version) {
        table.entries[id]->update(asset);
        table.versions[id] = asset.version;
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    // Meshes + materials come straight off the drawables.
    for (const DrawableData& d : view.drawables) {
        ensure(m_meshes, d.mesh, resources);
        ensure(m_materials, d.material, resources);
    }

    // Textures are discovered through each synced material's binding list.
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = getMaterial(d.material);
        if (!material) continue;
        for (const auto& binding : material->getTextureBindings()) {
            ensure(m_textures, binding.handle, resources);
        }
    }
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    return id < m_meshes.entries.size() ? m_meshes.entries[id].get() : nullptr;
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    return id < m_materials.entries.size() ? m_materials.entries[id].get() : nullptr;
}

const Core::Texture2D* GLView::getTexture(const TextureHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    if (id >= m_textures.entries.size() || !m_textures.entries[id]) return nullptr;
    return &m_textures.entries[id]->getTexture();
}

} // namespace Engine
