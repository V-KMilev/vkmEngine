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

namespace {

// The name each wrap mode is spelled by in a recipe. ClampToEdge is the default
// and never written, so the reader answers with it for an unknown name too.
const char* wrapName(TextureWrapMode wrap) {
    switch (wrap) {
        case TextureWrapMode::Repeat:         return "repeat";
        case TextureWrapMode::MirroredRepeat: return "mirror";
        case TextureWrapMode::ClampToEdge:    return "clamp";
        case TextureWrapMode::ClampToBorder:  return "border";
    }
    return "clamp";
}

} // namespace

nlohmann::json fileTextureRecipe(
    const std::string& ref,
    bool srgb,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
) {
    nlohmann::json source = {
        {"kind",            "file"},
        {"path",            ref},
        {"sRGB",            srgb},
        {"generateMipmaps", generateMipmaps},
    };
    if (filterOverride == TextureFilterOverride::Nearest) source["filter"] = "nearest";
    if (wrap != TextureWrapMode::ClampToEdge) source["wrap"] = wrapName(wrap);
    return source;
}

TextureFilterOverride textureFilterFromRecipe(const nlohmann::json& source) {
    return source.value("filter", std::string{}) == "nearest"
        ? TextureFilterOverride::Nearest
        : TextureFilterOverride::None;
}

TextureWrapMode textureWrapFromRecipe(const nlohmann::json& source) {
    const std::string wrap = source.value("wrap", std::string{});
    if (wrap == "repeat") return TextureWrapMode::Repeat;
    if (wrap == "mirror") return TextureWrapMode::MirroredRepeat;
    if (wrap == "border") return TextureWrapMode::ClampToBorder;
    return TextureWrapMode::ClampToEdge;
}

TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
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
    texture.params.filterOverride = filterOverride;
    texture.params.wrapS = wrap;
    texture.params.wrapT = wrap;
    texture.srgb = srgb;
    texture.filePath = ref;

    const size_t dataSize = width * height * channels;
    texture.pixelData.resize(dataSize);
    std::memcpy(texture.pixelData.data(), data, dataSize);

    stbi_image_free(data);

    LOG_VERBOSE("Loaded texture '%s' (%dx%d, %d channels, sRGB: %s)",
        ref.c_str(), width, height, channels, srgb ? "yes" : "no");

    texture.sourceJson() = fileTextureRecipe(ref, srgb, generateMipmaps, filterOverride, wrap);
    // The reference is the texture's name: the stable identity scene + material
    // references resolve by, and the path used to reload it.
    return resourceManager.add(std::move(texture), ref);
}

TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
) {
    // Relativised before the lookup, or one file requested under two spellings
    // becomes two assets.
    const std::string ref      = ProjectPaths::toProjectRelative(filePath);
    const std::string resolved = ProjectPaths::resolveProjectPath(ref).string();
    if (auto existing = resourceManager.findByName<TextureAsset>(ref)) {
        // A texture's identity is its path, so the second caller gets the first
        // one's decode settings. sRGB changes the pixels rather than the sampling,
        // so a request that disagrees says so instead of being answered quietly.
        if (resourceManager.get(existing).srgb != srgb) {
            LOG_WARNING("'%s' is already loaded as %s and is being asked for as %s; "
                        "the loaded one is returned unchanged",
                        ref.c_str(), resourceManager.get(existing).srgb ? "sRGB" : "linear",
                        srgb ? "sRGB" : "linear");
        }
        return existing;
    }

    // Stub asset: the finaliser overwrites only what the decode learned, so what
    // it cannot learn - mipmaps, sRGB, filter, wrap - is set here and survives
    // onto the finished asset. Dimensions arrive with the pixels.
    TextureAsset stub;
    stub.params.generateMipmaps = generateMipmaps;
    stub.params.filterOverride  = filterOverride;
    stub.params.wrapS           = wrap;
    stub.params.wrapT           = wrap;
    stub.srgb                   = srgb;
    stub.loading                = true;
    stub.filePath               = ref;
    stub.sourceJson() = fileTextureRecipe(ref, srgb, generateMipmaps, filterOverride, wrap);
    const TextureHandle handle = resourceManager.add(std::move(stub), ref);
    const uint64_t      uid    = resourceManager.get(handle).uid();

    // stb's orientation flag is a process-wide global, so it is set on the main
    // thread and before the task is queued - which orders it against the worker
    // that reads it, where two racing decodes would not be ordered at all.
    stbi_set_flip_vertically_on_load(true);

    // The task captures the resolved path and the asset's identity, nothing more:
    // everything ResourceManager-touching happens on the main thread when
    // AsyncLoaderSystem drains the completion.
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
