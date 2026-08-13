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

    if (id >= table.slots.size()) {
        table.slots.resize(id + 1);
    }

    // A freed slot can be recycled by a different asset that also starts at
    // version 1, so the version gate alone can't distinguish them. Rebuild from
    // scratch when the slot is empty or its generation moved on (recycled); an
    // in-place update() is only valid when it is the same asset (same generation)
    // with a newer version.
    auto& slot = table.slots[id];
    if (!slot.gl || slot.generation != generation) {
        slot.gl         = std::make_unique<GLT>(asset);
        slot.version    = asset.version;
        slot.generation = generation;
    } else if (slot.version != asset.version) {
        slot.gl->update(asset);
        slot.version = asset.version;
    }
}

void GLView::invalidateOnEpochChange(const ResourceManager& resources) {
    const uint64_t epoch = resources.epoch();
    if (epoch == m_epoch) return;

    // The incoming graph reuses the same handle indices and generations at
    // version 1, so ensure() cannot tell its assets apart from the outgoing
    // ones. Nothing here is salvageable: drop it all and let this sync repopulate.
    m_meshes.slots.clear();
    m_materials.slots.clear();
    m_textures.slots.clear();
    m_fontAtlases.slots.clear();
    m_epoch = epoch;
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    invalidateOnEpochChange(resources);

    // One walk: ensure each drawable's mesh + material, then discover the
    // material's textures off the entry we just synced. The material is
    // guaranteed present before its textures are needed, so no second pass.
    //
    // Drawables arrive clustered by material (the draw sort), so consecutive
    // repeats dominate at scale - skip the material + texture work when the
    // handle matches the previous drawable's.
    MaterialHandle lastMaterial;
    for (const DrawableData& d : view.drawables) {
        ensure(m_meshes, d.mesh, resources);
        if (d.material == lastMaterial) continue;
        lastMaterial = d.material;
        ensure(m_materials, d.material, resources);

        const GLMaterial* material = getMaterial(d.material);
        if (!material) continue;
        for (const auto& binding : material->getTextureBindings()) {
            ensure(m_textures, binding.handle, resources);
        }
    }

    // Font atlases live inside FontAssets (not the texture slot), so ensure
    // them straight off the overlay's text commands.
    for (const UIDrawCmd& cmd : view.ui.commands) {
        ensure(m_fontAtlases, cmd.font, resources);
    }
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    return id < m_meshes.slots.size() ? m_meshes.slots[id].gl.get() : nullptr;
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    return id < m_materials.slots.size() ? m_materials.slots[id].gl.get() : nullptr;
}

const Core::Texture2D* GLView::getTexture(const TextureHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    if (id >= m_textures.slots.size() || !m_textures.slots[id].gl) return nullptr;
    return &m_textures.slots[id].gl->getTexture();
}

const Core::Texture2D* GLView::getFontAtlas(const FontHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    if (id >= m_fontAtlases.slots.size() || !m_fontAtlases.slots[id].gl) return nullptr;
    return &m_fontAtlases.slots[id].gl->getTexture();
}

} // namespace Engine
