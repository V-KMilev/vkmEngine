#define VKM_LOG_CATEGORY "LOADER"

#include "import/texture_import.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>
#include "stb_image.h"

#include "vkm_log/logger.h"

#include "io/project/roots.h"
#include "decoded_image.h"
#include "vkm_platform/threading/thread_pool.h"
#include "io/asset/recipe/asset_source.h"
#include "io/asset/recipe/texture_recipe.h"
#include "resource/resource_manager.h"
#include "resource/asset/texture_params.h"
#include "resource/async_load_queue.h"

namespace Vkm::Engine {

namespace {

// Decode through stb by decodeChannels' rule. A normal map is decoded at three channels and
// packed to x and y in place, as stb's two are gray and alpha. Null, with stb's reason set, on
// failure; freed with stbi_image_free.
template<typename Info, typename Load>
unsigned char* decodeStored(
    TextureUsage usage,
    int& width,
    int& height,
    int& channels,
    Info&& info,
    Load&& load
) {
    int fileChannels = 0;
    if (!info(width, height, fileChannels)) return nullptr;
    channels = decodeChannels(fileChannels, usage);
    if (usage != TextureUsage::Normal) return load(width, height, channels);

    unsigned char* rgb = load(width, height, 3);
    if (!rgb) return nullptr;
    const size_t texels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < texels; ++i) {
        rgb[i * 2 + 0] = rgb[i * 3 + 0];
        rgb[i * 2 + 1] = rgb[i * 3 + 1];
    }
    return rgb;
}

unsigned char* decodeImage(
    const std::string& resolved,
    TextureUsage usage,
    int& width,
    int& height,
    int& channels
) {
    return decodeStored(
        usage,
        width,
        height,
        channels,
        [&](int& w, int& h, int& n) { return stbi_info(resolved.c_str(), &w, &h, &n) != 0; },
        [&](int& w, int& h, int wanted) {
            int fileChannels = 0;
            return stbi_load(resolved.c_str(), &w, &h, &fileChannels, wanted);
        }
    );
}

// Move stb's decode into @p texture, freeing it: its size, its formats for @p usage, its texels.
void takeDecoded(
    TextureAsset& texture,
    unsigned char* data,
    int width,
    int height,
    int channels,
    TextureUsage usage
) {
    texture.params.width          = static_cast<uint32_t>(width);
    texture.params.height         = static_cast<uint32_t>(height);
    texture.params.internalFormat = inferInternalFormat(channels, usage);
    texture.params.format         = inferFormat(channels);
    texture.params.type           = TexturePixelType::UnsignedByte;

    // Widened first: stb allows 2^24 a side, and an int product overflows long before.
    const size_t dataSize =
        static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    texture.pixelData.assign(data, data + dataSize);
    stbi_image_free(data);
}

} // namespace

bool decodeTextureFromMemory(const unsigned char* bytes, size_t size, TextureUsage usage, TextureAsset& out) {
    decodeImagesBottomUp();
    const int length = static_cast<int>(size);
    int width    = 0;
    int height   = 0;
    int channels = 0;
    unsigned char* data = decodeStored(
        usage,
        width,
        height,
        channels,
        [&](int& w, int& h, int& n) { return stbi_info_from_memory(bytes, length, &w, &h, &n) != 0; },
        [&](int& w, int& h, int wanted) {
            int fileChannels = 0;
            return stbi_load_from_memory(bytes, length, &w, &h, &fileChannels, wanted);
        }
    );
    if (!data) return false;

    takeDecoded(out, data, width, height, channels, usage);
    return true;
}

TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
) {
    // Named by the reference: an absolute name would bake this machine's tree into every scene.
    const std::string ref      = Roots::toProjectRelative(filePath);
    const std::string resolved = Roots::resolveProjectPath(ref).string();
    if (const TextureHandle existing = findLoadedTexture(ref, resourceManager, usage)) return existing;

    decodeImagesBottomUp();

    int width    = 0;
    int height   = 0;
    int channels = 0;
    unsigned char* data = decodeImage(resolved, usage, width, height, channels);

    if (!data) {
        LOG_ERROR("Failed to load texture from '%s': %s", resolved.c_str(), stbi_failure_reason());
        return TextureHandle{};
    }

    TextureAsset texture;
    texture.params.generateMipmaps = generateMipmaps;
    texture.params.filterOverride  = filterOverride;
    texture.params.wrapS           = wrap;
    texture.params.wrapT           = wrap;
    takeDecoded(texture, data, width, height, channels, usage);

    LOG_VERBOSE(
        "Loaded texture '%s' (%dx%d, %d channels, %s)",
        ref.c_str(),
        width,
        height,
        channels,
        Reflect::enumName(usage)
    );

    texture.sourceJson() = fileTextureRecipe(ref, usage, generateMipmaps, filterOverride, wrap);
    return resourceManager.add(std::move(texture), ref);
}

TextureHandle findLoadedTexture(
    const std::string& filePath,
    const ResourceManager& resourceManager,
    TextureUsage usage
) {
    const std::string ref = Roots::toProjectRelative(filePath);
    const TextureHandle existing = resourceManager.findByName<TextureAsset>(ref);
    if (!existing) return existing;
    const TextureUsage loaded = resourceManager.get(existing).usage();
    if (loaded != usage) {
        LOG_WARNING(
            "'%s' is already loaded as %s and is being asked for as %s; "
            "the loaded one is returned unchanged",
            ref.c_str(),
            Reflect::enumName(loaded),
            Reflect::enumName(usage)
        );
    }
    return existing;
}

TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
) {
    // Relativized first, or two spellings of one file become two assets.
    const std::string ref      = Roots::toProjectRelative(filePath);
    const std::string resolved = Roots::resolveProjectPath(ref).string();
    if (const TextureHandle existing = findLoadedTexture(ref, resourceManager, usage)) return existing;

    // Until the pixels arrive the format is the usage's four-channel one; the decode keeps the rest.
    TextureAsset stub;
    stub.params.internalFormat  = inferInternalFormat(4, usage);
    stub.params.generateMipmaps = generateMipmaps;
    stub.params.filterOverride  = filterOverride;
    stub.params.wrapS           = wrap;
    stub.params.wrapT           = wrap;
    stub.loading                = true;
    stub.sourceJson() = fileTextureRecipe(ref, usage, generateMipmaps, filterOverride, wrap);
    const TextureHandle handle = resourceManager.add(std::move(stub), ref);
    const uint64_t uid = resourceManager.get(handle).uid();

    // Captures only values: ResourceManager is touched on the main thread when AsyncLoaderSystem
    // drains the completion.
    const TextureParams params = resourceManager.get(handle).params;
    Platform::ThreadPool::get().addTask([handle, uid, resolved, usage, params]() {
        decodeImagesBottomUp();
        int w = 0, h = 0, channels = 0;
        unsigned char* data = decodeImage(resolved, usage, w, h, channels);

        TextureAsset decoded;
        if (data) {
            decoded.params = params;
            takeDecoded(decoded, data, w, h, channels, usage);
        } else {
            LOG_ERROR("Async texture decode failed for '%s': %s", resolved.c_str(), stbi_failure_reason());
        }
        AsyncLoadQueue::get().pushTexture({handle, uid, std::move(decoded)});
    });

    return handle;
}

} // namespace Vkm::Engine
