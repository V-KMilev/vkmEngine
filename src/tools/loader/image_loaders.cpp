#define VKM_LOG_CATEGORY "LOADER"

#include "loader/image_loaders.h"

#include <cstddef>

#include "logger.h"

// Declarations only: the STB_IMAGE_IMPLEMENTATION symbols come from the stb
// module, linked transitively via vkm_core.
#include "stb_image.h"

#include "io/project_paths.h"

namespace Vkm::Engine {

DecodedImage decodeImageRGBA(const std::string& filePath) {
    DecodedImage image;
    const std::string resolved = ProjectPaths::resolveProjectPath(filePath).string();

    // Bottom-up, which is GL's row order and what every loader here asks for. The
    // flag is process-wide and read inside the decode, so setting it the other way
    // flips whatever a worker is decoding at that moment.
    stbi_set_flip_vertically_on_load(true);

    int width    = 0;
    int height   = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(resolved.c_str(), &width, &height, &channels, 4);

    if (!data) {
        LOG_ERROR("Failed to decode image '%s': %s",
            resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);
    return image;
}

} // namespace Vkm::Engine
