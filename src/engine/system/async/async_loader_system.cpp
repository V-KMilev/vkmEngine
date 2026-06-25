#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "system/async/async_loader_system.h"

#include "logger.h"

#include "core/system.h"
#include "debug/profiler.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "resource/asset/texture_asset.h"
#include "resource/texture_format.h"
#include "system/async/async_load_queue.h"

namespace Engine {

namespace {

/// Infer engine-level internal + pixel format from channel count and sRGB.
/// Mirrors the helper in texture_loaders.cpp; kept local here so the system
/// has no dependency on the loader's TU.
TextureInternalFormat inferInternalFormat(int channels, bool srgb) {
    if (srgb) {
        return (channels == 3) ? TextureInternalFormat::SRGB8
                               : TextureInternalFormat::SRGBA8;
    }
    switch (channels) {
        case 1: return TextureInternalFormat::R8;
        case 2: return TextureInternalFormat::RG8;
        case 3: return TextureInternalFormat::RGB8;
        case 4:
        default: return TextureInternalFormat::RGBA8;
    }
}

TexturePixelFormat inferFormat(int channels) {
    switch (channels) {
        case 1: return TexturePixelFormat::R;
        case 2: return TexturePixelFormat::RG;
        case 3: return TexturePixelFormat::RGB;
        case 4:
        default: return TexturePixelFormat::RGBA;
    }
}

} // namespace

void AsyncLoaderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AsyncLoaderSystem");

    ResourceManager& rm = ctx.resources;

    // Textures: pixel data + format inferred from channel count.
    for (auto& c : AsyncLoadQueue::get().drainTextures()) {
        if (!c.handle) continue;

        // The asset may have been destroyed between push and drain (scene
        // swap, editor deletion). resourceManager.get on a dead handle is
        // UB; guard with a slot-level liveness check.
        if (!rm.isAlive(c.handle)) {
            LOG_VERBOSE("Texture handle %u dead before completion landed - dropping", c.handle.id());
            continue;
        }

        TextureAsset& asset = rm.edit(c.handle);

        if (!c.success || c.pixelData.empty()) {
            LOG_WARNING("Async texture decode failed for '%s' - leaving asset empty",
                asset.filePath.c_str());
            asset.loading = false;
            continue;
        }

        if (c.hasParams) {
            // Cooked texture: params (incl. format/sRGB/wrap/filter) are exact.
            asset.params = c.params;
            asset.srgb   = (c.params.internalFormat == TextureInternalFormat::SRGB8 ||
                            c.params.internalFormat == TextureInternalFormat::SRGBA8);
        } else {
            // stb-decoded texture: infer format from channel count + sRGB flag.
            asset.params.width          = c.width;
            asset.params.height         = c.height;
            asset.params.internalFormat = inferInternalFormat(c.channels, asset.srgb);
            asset.params.format         = inferFormat(c.channels);
            asset.params.type           = TexturePixelType::UnsignedByte;
        }
        asset.pixelData = std::move(c.pixelData);
        asset.loading   = false;
        rm.commit(c.handle);
    }

    // Meshes: vertex/index buffers + precomputed bounds. The worker did
    // the bounds computation alongside the vertex extraction so this loop
    // is pure move + commit.
    for (auto& c : AsyncLoadQueue::get().drainMeshes()) {
        if (!c.handle) continue;
        if (!rm.isAlive(c.handle)) {
            LOG_VERBOSE("Mesh handle %u dead before completion landed - dropping", c.handle.id());
            continue;
        }

        MeshAsset& asset = rm.edit(c.handle);

        if (!c.success || c.vertices.empty()) {
            LOG_WARNING("Async mesh decode failed for '%s' - leaving asset empty",
                asset.name.c_str());
            asset.loading = false;
            continue;
        }

        asset.vertices  = std::move(c.vertices);
        asset.indices   = std::move(c.indices);
        asset.boundsMin = c.boundsMin;
        asset.boundsMax = c.boundsMax;
        asset.loading   = false;
        rm.commit(c.handle);
    }
}

} // namespace Engine
