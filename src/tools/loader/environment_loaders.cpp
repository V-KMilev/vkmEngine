#define VKM_LOG_CATEGORY "LOADER"

#include "loader/environment_loaders.h"

#include <cstddef>
#include <string>

#include "stb_image.h"

#include "logger.h"

#include "io/project_paths.h"
#include "loader/image_loaders.h"

namespace Vkm::Engine {

HDRImage loadHDRImage(const std::string& filePath) {
    HDRImage image;
    const std::string resolved = ProjectPaths::resolveProjectPath(filePath).string();

    decodeImagesBottomUp();

    int width = 0;
    int height = 0;
    int channels = 0;
    float* data = stbi_loadf(resolved.c_str(), &width, &height, &channels, 3);

    if (!data) {
        LOG_ERROR("Failed to load HDR environment '%s': %s", resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);

    LOG_INFO("Loaded HDR environment '%s' (%dx%d equirect)", resolved.c_str(), width, height);
    return image;
}

} // namespace Vkm::Engine
