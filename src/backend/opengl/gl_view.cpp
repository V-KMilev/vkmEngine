#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_view.h"

#include <algorithm>
#include <type_traits>
#include <vector>

#include <GL/glew.h>

#include "logger.h"
#include "debug/profiler.h"

#include "gl_texture.h"

#include "resource/resource_manager.h"
#include "system/render/render_view.h"

#include "asset/gl_mesh.h"
#include "asset/gl_material.h"
#include "asset/gl_asset_texture.h"

namespace Vkm::Engine {

template <typename GLT, typename AssetT>
bool GLView::ensure(
    GLResourceTable<GLT>& table,
    const Handle<AssetT>& handle,
    const ResourceManager& resources
) {
    if (!handle) return false;

    const uint32_t id         = handle.id();
    const uint32_t generation = handle.key.generation;

    if (id >= table.slots.size()) {
        table.slots.resize(id + 1);
    }

    auto& slot = table.slots[id];
    if (slot.gl && slot.generation == generation && slot.checked == m_syncStamp) return false;

    const AssetT& asset = resources.get(handle);
    bool uploaded = true;
    if (!slot.gl || slot.generation != generation) {
        // A mesh is a range in the pool, and the pool is this mirror's.
        if constexpr (std::is_same_v<AssetT, MeshAsset>) {
            slot.gl = std::make_unique<GLT>(m_meshPool, asset);
        } else {
            slot.gl = std::make_unique<GLT>(asset);
        }
        slot.version    = asset.version();
        slot.generation = generation;
    } else if (slot.version != asset.version()) {
        slot.gl->update(asset);
        slot.version = asset.version();
    } else {
        uploaded = false;
    }
    // A texture built since the filtering last moved takes it now; nothing
    // walks the table again until the setting does. Font atlases keep their own.
    if constexpr (std::is_same_v<AssetT, TextureAsset>) {
        if (uploaded) slot.gl->applyFiltering(m_filtering, m_anisotropy);
    }
    slot.checked = m_syncStamp;
    return true;
}

template <typename GLT, typename AssetT>
const GLT* GLView::find(const GLResourceTable<GLT>& table, const Handle<AssetT>& handle) const {
    if (!handle || handle.id() >= table.slots.size()) return nullptr;
    const auto& slot = table.slots[handle.id()];
    return slot.generation == handle.key.generation ? slot.gl.get() : nullptr;
}

void GLView::invalidate() {
    // m_reportedMissing goes too: a name failing again in the new graph deserves a fresh warning.
    m_meshes.slots.clear();
    m_materials.slots.clear();
    m_textures.slots.clear();
    m_fontAtlases.slots.clear();
    m_reportedMissing.clear();
}

void GLView::setTextureFiltering(TextureFiltering mode, float maxAnisotropy) {
    if (mode == m_filtering && maxAnisotropy == m_anisotropy) return;
    m_filtering  = mode;
    m_anisotropy = maxAnisotropy;
    for (auto& slot : m_textures.slots) {
        if (!slot.gl) continue;
        slot.gl->applyFiltering(mode, maxAnisotropy);
    }
}

void GLView::ensureTexture(const TextureHandle& handle, const ResourceManager& resources) {
    if (!handle) return;
    // Asked between syncs, for the texture as it is now, not as the last sync saw it.
    ++m_syncStamp;
    ensure(m_textures, handle, resources);
    reportIfMissing(handle, resources);
}

void GLView::reportIfMissing(const TextureHandle& handle, const ResourceManager& resources) {
    const TextureAsset& asset = resources.get(handle);
    // Still in flight is not a failure; the placeholder covers the gap.
    if (asset.loading || getTexture(handle)) return;

    // Settled empty: the decode failed or the file was never there. Said once per asset.
    if (!m_reportedMissing.insert(handle.id()).second) return;

    LOG_WARNING("Texture '%s' has no pixels - drawing the missing-texture placeholder", asset.name().c_str());
}

void GLView::ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources) {
    // Its maps were checked with it the first time this stamp reached it.
    if (!ensure(m_materials, handle, resources)) return;

    const GLMaterial* material = getMaterial(handle);
    if (!material) return;
    for (const auto& binding : material->getTextureBindings()) {
        ensure(m_textures, binding.handle, resources);
        reportIfMissing(binding.handle, resources);
    }
}

void GLView::sync(const RenderView& view, const ResourceManager& resources) {
    // Naming every RenderView member keeps the walk below complete: add one and this stops
    // compiling until someone says whether it carries an asset handle.
    [[maybe_unused]] const auto& [
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        surfaceWidth,
        surfaceHeight,
        camera,
        hasCamera,
        objects,
        lights,
        probes,
        decals,
        particlesAdditive,
        particlesAlpha,
        irradianceVolume,
        hasIrradianceVolume,
        skinMatrices,
        settings,
        environment,
        ui,
        splash,
        worldEpoch
    ] = view;

    // A new stamp, so each asset is checked once however many objects share it.
    ++m_syncStamp;

    if (objects) {
        const std::vector<ObjectDraw>& draws = objects->draws;
        {
            PROFILE_SCOPE("SyncAssets/Drawables");
            for (const uint32_t object : objects->visible) {
                ensure(m_meshes, draws[object].mesh, resources);
                ensureMaterial(draws[object].material, resources);
            }
        }

        // The scene list's materials are left out (see GLSceneCapture), but
        // for the casters, which lead the list: a cutout casts through its map.
        {
            PROFILE_SCOPE("SyncAssets/SceneMeshes");
            const size_t casters = std::min<size_t>(objects->casterCount, objects->scene.size());
            for (size_t i = 0; i < objects->scene.size(); ++i) {
                const uint32_t object = objects->scene[i];
                ensure(m_meshes, draws[object].mesh, resources);
                if (i < casters) ensureMaterial(draws[object].material, resources);
            }
        }
    }

    // Decals are gathered scene-wide, and a decal's material is usually its own.
    for (const DecalData& decal : decals) {
        ensureMaterial(decal.material, resources);
    }

    // Font atlases live inside FontAssets, so ensure them off the text commands; images are
    // ordinary textures.
    if (ui) {
        for (const UIDrawCmd& cmd : ui->commands) {
            ensure(m_fontAtlases, cmd.font, resources);
            if (!cmd.image) continue;
            ensure(m_textures, cmd.image, resources);
            reportIfMissing(cmd.image, resources);
        }
    }
}

const GLMesh* GLView::getMesh(const MeshHandle& handle) const {
    return find(m_meshes, handle);
}

const GLMaterial* GLView::getMaterial(const MaterialHandle& handle) const {
    return find(m_materials, handle);
}

const Vkm::GL::Texture2D* GLView::getTexture(const TextureHandle& handle) const {
    // Pixels never arrived: absent, so the caller substitutes the placeholder.
    const GLTexture* texture = find(m_textures, handle);
    return texture && texture->hasPixels() ? &texture->getTexture() : nullptr;
}

bool GLView::holdsPixels(const TextureHandle& handle, uint64_t version) const {
    return getTexture(handle) && m_textures.slots[handle.id()].version == version;
}

uint64_t GLView::textureUploadId(const TextureHandle& handle) const {
    if (!getTexture(handle)) return 0;
    return m_textures.slots[handle.id()].gl->uploadId();
}

const Vkm::GL::Texture2D* GLView::getFontAtlas(const FontHandle& handle) const {
    const GLTexture* atlas = find(m_fontAtlases, handle);
    return atlas && atlas->hasPixels() ? &atlas->getTexture() : nullptr;
}

const Vkm::GL::Texture2D& GLView::missingTexture() const {
    if (m_missingTexture) return *m_missingTexture;

    // Magenta because nothing in a PBR scene is legitimately that colour; a checker because a
    // flat fill can pass for an authored material.
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
    // Nearest, no mips: a hard-edged grid at every distance, not flat magenta far away.
    params.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    params.data            = pixels.data();

    m_missingTexture = std::make_unique<Vkm::GL::Texture2D>("missing", params);
    return *m_missingTexture;
}

} // namespace Vkm::Engine
