#include "loader/environment_loaders.h"

#include <cstddef>
#include <filesystem>

#include "logger.h"

// stb_image is header-only; STB_IMAGE_IMPLEMENTATION is defined once in
// texture_loaders.cpp (same EngineTools target), so include declarations only.
#include "stb_image.h"

namespace Engine {

namespace {
    // The editor stores asset paths relative to the project root (e.g.
    // "assets/envs/environment_v3.hdr"), but the process working directory is
    // not the project root, so a bare fopen fails ("can't fopen" -> IBL stays
    // off). Resolve relatives against APP_ROOT_DIR (the documented engine
    // convention - matches scene IO / model import). Absolute paths (the
    // main.cpp default) pass through unchanged.
    std::string resolveAssetPath(const std::string& p) {
#ifdef APP_ROOT_DIR
        std::filesystem::path fp(p);
        if (fp.is_relative()) {
            return (std::filesystem::path(APP_ROOT_DIR) / fp)
                       .lexically_normal().string();
        }
#endif
        return p;
    }
}

HDRImage loadHDRImage(const std::string& filePath) {
    HDRImage image;
    const std::string resolved = resolveAssetPath(filePath);

    // GL texture origin is bottom-left; flip so the sky lands at v = 1, which
    // is what the equirect bake shader expects (matches texture_loaders.cpp).
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    float* data = stbi_loadf(resolved.c_str(), &width, &height, &channels, 3);

    if (!data) {
        LOG_ERROR("Failed to load HDR environment '%s': %s",
            resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);

    LOG_INFO("Loaded HDR environment '%s' (%dx%d equirect)",
        resolved.c_str(), width, height);
    return image;
}

LDRImage loadColorLUT(const std::string& filePath) {
    LDRImage image;
    const std::string resolved = resolveAssetPath(filePath);

    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(resolved.c_str(), &width, &height, &channels, 4);

    if (!data) {
        LOG_ERROR("Failed to load color LUT '%s': %s",
            resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);

    LOG_INFO("Loaded color LUT '%s' (%dx%d)", resolved.c_str(), width, height);
    return image;
}

} // namespace Engine
