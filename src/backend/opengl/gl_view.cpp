#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_view.h"

#include <vector>

#include <GL/glew.h>

#include "logger.h"

#include "texture/gl_texture.h"

#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "data/gl_mesh.h"
#include "data/gl_material.h"
#include "data/gl_texture.h"

namespace Vkm::Engine {

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

void GLView::invalidate() {
    // The incoming graph reuses the same handle indices and generations at
    // version 1, so ensure() cannot tell its assets apart from the outgoing
    // ones. Nothing here is salvageable: drop it all and let the next sync
    // repopulate. m_reportedMissing goes too - a name that failed to load in the
    // old graph deserves a fresh warning if it fails again in the new one.
    m_meshes.slots.clear();
    m_materials.slots.clear();
    m_textures.slots.clear();
    m_fontAtlases.slots.clear();
    m_reportedMissing.clear();
}

void GLView::reportIfMissing(const TextureHandle& handle, const ResourceManager& resources) {
    const TextureAsset& asset = resources.get(handle);
    // Still in flight is not a failure - it resolves on a later frame, and the
    // placeholder covers the gap meanwhile.
    if (asset.loading || !asset.pixelData.empty()) return;

    // Settled with nothing in it: the decode failed or the file was never
    // there. Say so once per asset - the checkerboard shows that something is
    // wrong, this says which file, which is the part you cannot see on screen.
    if (!m_reportedMissing.insert(handle.id()).second) return;

    LOG_WARNING("Texture '%s' has no pixels (path '%s') - drawing the missing-texture placeholder",
        asset.name.c_str(), asset.filePath.c_str());
}

void GLView::ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources) {
    ensure(m_materials, handle, resources);

    const GLMaterial* material = getMaterial(handle);
    if (!material) return;
    for (const auto& binding : material->getTextureBindings()) {
        ensure(m_textures, binding.handle, resources);
        reportIfMissing(binding.handle, resources);
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    // Drawables arrive clustered by (material, mesh) - that is the draw sort -
    // so consecutive repeats dominate at scale and the previous handle is worth
    // remembering.
    MaterialHandle lastMaterial;
    MeshHandle     lastMesh;
    for (const DrawableData& d : view.drawables) {
        if (d.mesh != lastMesh) {
            lastMesh = d.mesh;
            ensure(m_meshes, d.mesh, resources);
        }
        if (d.material == lastMaterial) continue;
        lastMaterial = d.material;
        ensureMaterial(d.material, resources);
    }

    // Casters are gathered scene-wide, not from the visible set, so an
    // off-screen occluder's mesh - or the far-LOD variant visibility picked for
    // it - may appear in no drawable at all, and the shadow pass answers a mesh
    // it cannot resolve by drawing nothing. They arrive in storage order rather
    // than sorted, so the repeat-skip is incidental here, but the list is
    // scene-sized and the compare is free.
    MeshHandle lastCasterMesh;
    for (const ShadowCasterData& caster : view.shadowCasters) {
        if (caster.mesh == lastCasterMesh) continue;
        lastCasterMesh = caster.mesh;
        ensure(m_meshes, caster.mesh, resources);
    }

    // Decals are gathered scene-wide too, and a decal material is usually its
    // own (a scorch, a bullet hole) rather than one some visible drawable
    // happens to share - so without this walk the decal pass skips every one.
    for (const DecalData& decal : view.decals) {
        ensureMaterial(decal.material, resources);
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

const Vkm::GL::Texture2D* GLView::getTexture(const TextureHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    if (id >= m_textures.slots.size() || !m_textures.slots[id].gl) return nullptr;
    // A texture whose pixels never arrived is not usable data: report it absent
    // so the caller substitutes the placeholder instead of sampling undefined
    // contents.
    if (!m_textures.slots[id].gl->hasPixels()) return nullptr;
    return &m_textures.slots[id].gl->getTexture();
}

const Vkm::GL::Texture2D& GLView::missingTexture() const {
    if (m_missingTexture) return *m_missingTexture;

    // 8x8 magenta-on-black checker. Magenta because nothing in a PBR scene is
    // legitimately that colour, and a checker because a flat fill can pass for
    // an authored material while a grid at any scale reads as "not a texture".
    constexpr uint32_t SIZE  = 8;
    constexpr uint32_t CHECK = 2;   // pixels per square
    std::vector<uint8_t> pixels(SIZE * SIZE * 4);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const bool lit = ((x / CHECK) + (y / CHECK)) % 2 == 0;
            uint8_t* p = &pixels[(y * SIZE + x) * 4];
            p[0] = lit ? 255 : 0;
            p[1] = 0;
            p[2] = lit ? 255 : 0;
            p[3] = 255;
        }
    }

    Vkm::GL::Texture2DParams params;
    params.width           = SIZE;
    params.height          = SIZE;
    params.internalFormat  = GL_SRGB8_ALPHA8;  // sampled as colour, like any albedo map
    params.format          = GL_RGBA;
    params.type            = GL_UNSIGNED_BYTE;
    params.wrapS           = Vkm::GL::TextureWrap::Repeat;
    params.wrapT           = Vkm::GL::TextureWrap::Repeat;
    // Nearest, and no mips: the point is to stay a hard-edged grid at every
    // distance rather than blurring into flat magenta far away.
    params.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    params.data            = pixels.data();

    m_missingTexture = std::make_unique<Vkm::GL::Texture2D>("missing", params);
    return *m_missingTexture;
}

const Vkm::GL::Texture2D* GLView::getFontAtlas(const FontHandle& handle) const {
    if (!handle) return nullptr;
    const uint32_t id = handle.id();
    if (id >= m_fontAtlases.slots.size() || !m_fontAtlases.slots[id].gl) return nullptr;
    return &m_fontAtlases.slots[id].gl->getTexture();
}

} // namespace Vkm::Engine
