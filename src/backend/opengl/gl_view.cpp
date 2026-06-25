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

    const uint32_t id         = handle.id();
    const uint32_t generation = handle.key.generation;
    const AssetT& asset = resources.get(handle);

    if (id >= table.entries.size()) {
        table.entries.resize(id + 1);
        table.versions.resize(id + 1, 0);
        table.generations.resize(id + 1, 0);
    }

    // A freed slot can be recycled by a different asset that also starts at
    // version 1, so the version gate alone can't distinguish them. Rebuild from
    // scratch when the slot is empty or its generation moved on (recycled); an
    // in-place update() is only valid when it is the same asset (same generation)
    // with a newer version.
    if (!table.entries[id] || table.generations[id] != generation) {
        table.entries[id]     = std::make_unique<GLT>(asset);
        table.versions[id]    = asset.version;
        table.generations[id] = generation;
    } else if (table.versions[id] != asset.version) {
        table.entries[id]->update(asset);
        table.versions[id] = asset.version;
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    // One walk: ensure each drawable's mesh + material, then discover the
    // material's textures off the entry we just synced. The material is
    // guaranteed present before its textures are needed, so no second pass.
    for (const DrawableData& d : view.drawables) {
        ensure(m_meshes, d.mesh, resources);
        ensure(m_materials, d.material, resources);

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
