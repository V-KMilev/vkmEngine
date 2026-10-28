#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_view.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

#include <GL/glew.h>

#include "logger.h"
#include "debug/profiler.h"

#include "gl_compute_shader.h"
#include "gl_texture.h"

#include "core/fnv1a.h"
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
    m_charts.clear();
    m_syncedRevision = 0;
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

void GLView::ensureDrawn(
    const MeshHandle& mesh,
    const MaterialHandle& material,
    const ResourceManager& resources
) {
    // Asked between syncs, for the assets as they are now, not as the last sync saw them.
    ++m_syncStamp;
    ensure(m_meshes, mesh, resources);
    ensureMaterial(material, resources);
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

    GLMaterial* material = m_materials.slots[handle.id()].gl.get();
    if (!material) return;
    for (const auto& binding : material->getTextureBindings()) {
        ensure(m_textures, binding.handle, resources);
        reportIfMissing(binding.handle, resources);
    }
    if (material->layered()) readyLayers(resources.get(handle), *material, resources);
}

void GLView::readyLayers(const MaterialAsset& asset, GLMaterial& material, const ResourceManager& resources) {
    std::array<const MaterialAsset*, MaterialAsset::MAX_LAYERS>  values{};
    std::array<GLLayerMaps::Layer, MaterialAsset::MAX_LAYERS>    maps{};
    uint64_t key = fnv1a64Bytes(nullptr, 0);
    const auto mix = [&](uint64_t value) { key = fnv1a64Bytes(&value, sizeof(value), key); };
    mix(asset.version());
    for (size_t i = 0; i < asset.layers.size(); ++i) {
        const MaterialHandle& layer = asset.layers[i];
        mix(layer ? layer.id() + 1u : 0u);
        if (!layer) continue;
        // Its maps upload with it; a layer's own layers, were it layered, are not read.
        ensureMaterial(layer, resources);
        const MaterialAsset& value   = resources.get(layer);
        const TextureHandle  combined = value.aoMetallicRoughnessTexture;
        const TextureHandle  surface  = combined ? combined : value.metallicRoughnessTexture;
        values[i] = &value;
        maps[i]   = {
            getTexture(value.albedoTexture),
            getTexture(value.normalTexture),
            getTexture(surface),
            !combined && static_cast<bool>(surface)
        };
        mix(value.version());
        mix(textureUploadId(value.albedoTexture));
        mix(textureUploadId(value.normalTexture));
        mix(textureUploadId(surface));
    }
    if (!m_layerCopy) buildPrograms();
    material.updateLayers(asset, values, maps, key, *m_layerCopy);
}

void GLView::buildPrograms() {
    m_layerCopy = std::make_unique<Vkm::GL::ComputeShader>("shaders/material/layer_copy");
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
        frameSeconds,
        bakeBudget,
        worldSeconds,
        worldArriving,
        camera,
        hasCamera,
        objects,
        lights,
        probes,
        planarReflectors,
        decals,
        particlesAdditive,
        particlesAlpha,
        particlesDistortion,
        stripPoints,
        stripsAdditive,
        stripsAlpha,
        stripsDistortion,
        particleTextures,
        irradianceVolume,
        hasIrradianceVolume,
        ocean,
        skinMatrices,
        debug,
        settings,
        environment,
        ui,
        splash,
        cover,
        loadingOpacity,
        loadingContent,
        loadingProgress,
        loadingWater,
        pointer,
        worldEpoch
    ] = view;

    // A new stamp, so each asset is checked once however many objects share it.
    ++m_syncStamp;

    if (objects) {
        const std::vector<ObjectDraw>& draws = objects->draws;
        // Props are placed in runs, so a handle is mostly the one before it: checked once a run.
        MeshHandle     runMesh;
        MaterialHandle runMaterial;
        const auto ensureDraw = [&](const ObjectDraw& draw) {
            if (draw.mesh != runMesh) {
                ensure(m_meshes, draw.mesh, resources);
                runMesh = draw.mesh;
            }
            if (draw.material != runMaterial) {
                ensureMaterial(draw.material, resources);
                runMaterial = draw.material;
            }
        };
        {
            PROFILE_SCOPE("SyncAssets/Drawables");
            for (const uint32_t object : objects->visible) ensureDraw(draws[object]);
        }

        // A fixed object's given mesh too, whatever level the eye picked: the lightmap bake draws it,
        // and the level reads the bake only through charts laid out as its.
        const std::vector<MeshHandle>& bases = objects->bases;
        MeshHandle runBase;
        const auto ensureObject = [&](uint32_t object) {
            ensureDraw(draws[object]);
            if (!draws[object].fixed || object >= bases.size()) return;
            const MeshHandle& given = bases[object];
            if (given == draws[object].mesh) return;
            if (given != runBase) {
                ensure(m_meshes, given, resources);
                runBase = given;
            }
            checkCharts(draws[object].mesh, given, resources);
        };

        // The whole scene's meshes and materials, not only what the camera sees, so an object first
        // turned to uploads nothing that frame. Walked whole when the objects or the assets changed
        // (any ResourceManager commit, swap or replace); otherwise only the changed objects.
        {
            PROFILE_SCOPE("SyncAssets/Scene");
            const bool whole = objects->allChanged || objects->serial != m_syncedSerial + 1
                || resources.revision() != m_syncedRevision;
            m_syncedSerial = objects->serial;
            if (whole) {
                for (const uint32_t object : objects->scene) ensureObject(object);
                m_syncedRevision = resources.revision();
            } else {
                for (const uint32_t object : objects->changed) ensureObject(object);
            }
        }
    }

    // Decals are gathered scene-wide, and a decal's material is usually its own.
    for (const DecalData& decal : decals) {
        ensureMaterial(decal.material, resources);
    }

    for (const TextureHandle& texture : particleTextures) {
        ensure(m_textures, texture, resources);
        reportIfMissing(texture, resources);
    }

    // Font atlases live inside FontAssets, so ensure them off the text commands; images are
    // ordinary textures. The cover is drawn as the UI is.
    for (const UIDrawData* overlay : {ui, cover}) {
        if (!overlay) continue;
        for (const UIDrawCmd& cmd : overlay->commands) {
            ensure(m_fontAtlases, cmd.font, resources);
            if (!cmd.image) continue;
            ensure(m_textures, cmd.image, resources);
            reportIfMissing(cmd.image, resources);
        }
    }
}

void GLView::checkCharts(const MeshHandle& drawn, const MeshHandle& given, const ResourceManager& resources) {
    const GLMesh* drawnMesh = getMesh(drawn);
    const GLMesh* givenMesh = getMesh(given);
    if (!drawnMesh || !givenMesh) return;
    ChartsCheck& check = m_charts[(static_cast<uint64_t>(drawn.id()) << 32) | given.id()];
    if (check.drawnUpload == drawnMesh->uploadId() && check.givenUpload == givenMesh->uploadId()) return;
    check.drawnUpload = drawnMesh->uploadId();
    check.givenUpload = givenMesh->uploadId();

    // Bit for bit: a decimation keeps the coordinates of the vertices it keeps, and a terrain chunk's
    // levels lay one grid out alike.
    const MeshAsset* reader = resources.tryGet(drawn);
    const MeshAsset* source = resources.tryGet(given);
    check.reads = false;
    if (!reader || !source || reader->lightmapUVs.empty()) return;
    static_assert(sizeof(glm::vec2) == sizeof(uint64_t), "a coordinate is compared as its bits");
    const auto bitsOf = [](const glm::vec2& uv) {
        uint64_t bits = 0;
        std::memcpy(&bits, &uv, sizeof(bits));
        return bits;
    };
    std::vector<uint64_t> charts(source->lightmapUVs.size());
    std::transform(source->lightmapUVs.begin(), source->lightmapUVs.end(), charts.begin(), bitsOf);
    std::sort(charts.begin(), charts.end());
    const auto inCharts = [&](const glm::vec2& uv) {
        return std::binary_search(charts.begin(), charts.end(), bitsOf(uv));
    };
    check.reads = std::all_of(reader->lightmapUVs.begin(), reader->lightmapUVs.end(), inCharts);
}

bool GLView::readsChartsOf(const MeshHandle& drawn, const MeshHandle& given) const {
    if (drawn == given) return true;
    const GLMesh* drawnMesh = getMesh(drawn);
    const GLMesh* givenMesh = getMesh(given);
    if (!drawnMesh || !givenMesh) return false;
    const auto found = m_charts.find((static_cast<uint64_t>(drawn.id()) << 32) | given.id());
    if (found == m_charts.end()) return false;
    const ChartsCheck& check = found->second;
    return check.reads && check.drawnUpload == drawnMesh->uploadId()
        && check.givenUpload == givenMesh->uploadId();
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

    // Magenta because nothing in a PBR scene is legitimately that color; a checker because a
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
    params.internalFormat  = GL_SRGB8_ALPHA8;  // sampled as color, like any albedo map
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
