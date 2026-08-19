#define VKM_LOG_CATEGORY "LOADER"

#include "loader/texture_loaders.h"

#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/project_paths.h"
#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "resource/texture_format.h"
#include "system/async/async_load_queue.h"

// stb_image's implementation is provided once by the stb module (libstb,
// linked via vkm_core); here we need only the declarations.
#include "stb_image.h"

namespace Vkm::Engine {

TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps
) {
    // The reference is what the asset is named and recorded by; the resolved
    // path is only what stb opens. An absolute name would bake the authoring
    // machine's directory tree into every scene, material and library filename.
    const std::string  ref      = ProjectPaths::toProjectRelative(filePath);
    const std::string  resolved = ProjectPaths::resolveProjectPath(ref).string();

    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(resolved.c_str(), &width, &height, &channels, 0);

    if (!data) {
        LOG_ERROR("Failed to load texture from '%s': %s", resolved.c_str(), stbi_failure_reason());
        return TextureHandle{};
    }

    TextureAsset texture;
    texture.params.width = static_cast<uint32_t>(width);
    texture.params.height = static_cast<uint32_t>(height);
    texture.params.internalFormat = inferInternalFormat(channels, srgb);
    texture.params.format = inferFormat(channels);
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = generateMipmaps;
    texture.srgb = srgb;
    texture.filePath = ref;

    const size_t dataSize = width * height * channels;
    texture.pixelData.resize(dataSize);
    std::memcpy(texture.pixelData.data(), data, dataSize);

    stbi_image_free(data);

    LOG_VERBOSE("Loaded texture '%s' (%dx%d, %d channels, sRGB: %s)",
        ref.c_str(), width, height, channels, srgb ? "yes" : "no");

    // The reference is the texture's name: the stable identity scene + material
    // references resolve by, and the path used to reload it.
    texture.name         = ref;
    texture.sourceJson() = {
        {"kind",           "file"},
        {"path",           ref},
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
    // The reference is the stable identity: a repeat request hands back the same
    // handle even while the first decode is in flight. The caller can bind it
    // right away; the asset just won't have pixels yet. Relativised before the
    // lookup, or one file requested under two spellings becomes two assets.
    const std::string ref      = ProjectPaths::toProjectRelative(filePath);
    const std::string resolved = ProjectPaths::resolveProjectPath(ref).string();
    if (auto existing = resourceManager.findByName<TextureAsset>(ref)) return existing;

    // Stub asset: dimensions filled in by the finaliser once decode is done.
    // The mipmap + sRGB flags do need to be set up-front since the asset
    // serializer round-trips them.
    TextureAsset stub;
    stub.params.generateMipmaps = generateMipmaps;
    stub.srgb                   = srgb;
    stub.loading                = true;
    stub.filePath               = ref;
    stub.name                   = ref;
    stub.sourceJson() = {
        {"kind",            "file"},
        {"path",            ref},
        {"sRGB",            srgb},
        {"generateMipmaps", generateMipmaps},
    };
    const TextureHandle handle = resourceManager.add(std::move(stub));
    const uint64_t      uid    = resourceManager.get(handle).uid;

    // stb's orientation flag is a process-wide global, so it is set here on the
    // main thread rather than inside the task. Two decodes racing on it would
    // silently hand one of them the wrong orientation - harmless only for as
    // long as every caller wants the same value, which is not a property worth
    // depending on. Setting it before the task is queued orders it against the
    // worker that will read it.
    stbi_set_flip_vertically_on_load(true);

    // The task captures only the resolved path + the asset's identity:
    // everything ResourceManager-touching happens on the main thread in
    // AsyncLoaderSystem when the completion is drained, and the project root a
    // reference resolves against is main-thread state.
    ThreadPool::get().addTask([handle, uid, resolved]() {
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(resolved.c_str(), &w, &h, &channels, 0);

        TextureLoadCompletion completion;
        completion.handle   = handle;
        completion.assetUid = uid;
        if (!data) {
            LOG_ERROR("Async texture decode failed for '%s': %s",
                resolved.c_str(), stbi_failure_reason());
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

} // namespace Vkm::Engine
