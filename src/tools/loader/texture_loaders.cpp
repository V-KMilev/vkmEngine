#define VKM_LOG_CATEGORY "LOADER"

#include "texture_loaders.h"

#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

#include "logger.h"
#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "system/async/async_load_queue.h"

// stb_image's implementation is provided once by the stb module (libstb,
// linked via EngineCore); here we need only the declarations.
#include "stb_image.h"

namespace Engine {

namespace {
/**
 * @brief Helper to infer texture internal format from channel count and sRGB flag.
 */
TextureInternalFormat inferInternalFormat(int channels, bool srgb) {
    if (srgb) {
        switch (channels) {
            case 3: return TextureInternalFormat::SRGB8;
            case 4: return TextureInternalFormat::SRGBA8;
            default: return TextureInternalFormat::SRGBA8;
        }
    }
    switch (channels) {
        case 1: return TextureInternalFormat::R8;
        case 2: return TextureInternalFormat::RG8;
        case 3: return TextureInternalFormat::RGB8;
        case 4: return TextureInternalFormat::RGBA8;
        default: return TextureInternalFormat::RGBA8;
    }
}

/**
 * @brief Helper to infer texture pixel format from channel count.
 */
TexturePixelFormat inferFormat(int channels) {
    switch (channels) {
        case 1: return TexturePixelFormat::R;
        case 2: return TexturePixelFormat::RG;
        case 3: return TexturePixelFormat::RGB;
        case 4: return TexturePixelFormat::RGBA;
        default: return TexturePixelFormat::RGBA;
    }
}
} // namespace

TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps
) {
    // Load image using stb_image
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

    if (!data) {
        LOG_ERROR("Failed to load texture from '%s': %s", filePath.c_str(), stbi_failure_reason());
        return TextureHandle{}; // Return invalid handle
    }

    // Create TextureAsset
    TextureAsset texture;
    texture.params.width = static_cast<uint32_t>(width);
    texture.params.height = static_cast<uint32_t>(height);
    texture.params.internalFormat = inferInternalFormat(channels, srgb);
    texture.params.format = inferFormat(channels);
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = generateMipmaps;
    texture.srgb = srgb;
    texture.filePath = filePath;

    // Copy pixel data
    const size_t dataSize = width * height * channels;
    texture.pixelData.resize(dataSize);
    std::memcpy(texture.pixelData.data(), data, dataSize);

    // Free stb_image data
    stbi_image_free(data);

    LOG_VERBOSE("Loaded texture '%s' (%dx%d, %d channels, sRGB: %s)",
        filePath.c_str(), width, height, channels, srgb ? "yes" : "no");

    // The file path is the texture's name: the stable identity scene + material
    // references resolve by, and the path used to reload it.
    texture.name         = filePath;
    texture.sourceJson() = {
        {"kind",           "file"},
        {"path",           filePath},
        {"sRGB",           srgb},
        {"generateMipmaps", generateMipmaps},
    };
    return resourceManager.add(std::move(texture));
}

TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps
) {
    // Path is the stable identity. If we've already requested this exact
    // path before, return the same handle - even if the prior request is
    // still in flight. Caller is free to bind/use the handle immediately;
    // the asset just won't have pixels yet.
    if (auto existing = resourceManager.findByName<TextureAsset>(filePath)) return existing;

    // Stub asset: dimensions filled in by the finaliser once decode is done.
    // The mipmap + sRGB flags do need to be set up-front since the asset
    // serializer round-trips them.
    TextureAsset stub;
    stub.params.generateMipmaps = generateMipmaps;
    stub.srgb                   = srgb;
    stub.loading                = true;
    stub.filePath               = filePath;
    stub.name                   = filePath;
    stub.sourceJson() = {
        {"kind",            "file"},
        {"path",            filePath},
        {"sRGB",            srgb},
        {"generateMipmaps", generateMipmaps},
    };
    const TextureHandle handle = resourceManager.add(std::move(stub));

    // Spawn the decode on a worker. We capture only the path + handle +
    // flip flag - everything ResourceManager-touching happens on the main
    // thread in AsyncLoaderSystem when the completion is drained.
    ThreadPool::get().addTask([handle, filePath]() {
        stbi_set_flip_vertically_on_load(true);
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &channels, 0);

        TextureLoadCompletion completion;
        completion.handle = handle;
        if (!data) {
            LOG_ERROR("Async texture decode failed for '%s': %s",
                filePath.c_str(), stbi_failure_reason());
            completion.success = false;
            AsyncLoadQueue::get().pushTexture(std::move(completion));
            return;
        }

        const size_t dataSize = static_cast<size_t>(w) * static_cast<size_t>(h) * channels;
        completion.width    = static_cast<uint32_t>(w);
        completion.height   = static_cast<uint32_t>(h);
        completion.channels = channels;
        completion.pixelData.assign(data, data + dataSize);
        completion.success  = true;
        stbi_image_free(data);

        AsyncLoadQueue::get().pushTexture(std::move(completion));
    });

    return handle;
}

} // namespace Engine
