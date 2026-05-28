#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "system/async/async_loader_system.h"

#include "logger.h"

#include "core/system.h"
#include "debug/profiler.h"
#include "resource/resource_manager.h"
#include "resource/texture_asset.h"
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

    auto completions = AsyncLoadQueue::get().drainTextures();
    if (completions.empty()) return;

    ResourceManager& rm = ctx.resources;

    for (auto& c : completions) {
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

        asset.params.width          = c.width;
        asset.params.height         = c.height;
        asset.params.internalFormat = inferInternalFormat(c.channels, asset.srgb);
        asset.params.format         = inferFormat(c.channels);
        asset.params.type           = TexturePixelType::UnsignedByte;
        asset.pixelData             = std::move(c.pixelData);
        asset.loading               = false;
        rm.commit(c.handle);
    }
}

} // namespace Engine
