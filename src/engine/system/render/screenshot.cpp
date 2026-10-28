#include "debug/screenshot.h"

// The writer's implementation, compiled once here; its initialisers trip -Wextra.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace Vkm::Engine {

bool writeScreenshotPng(
    const std::string& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgb
) {
    if (width == 0 || height == 0 || rgb.size() < static_cast<size_t>(width) * height * 3) return false;

    // The flip has no getter, but its state lives in this translation unit.
    const int level  = stbi_write_png_compression_level;
    const int filter = stbi_write_force_png_filter;
    const int flip   = stbi__flip_vertically_on_write;
    stbi_write_png_compression_level = 1;
    stbi_write_force_png_filter      = 0;
    stbi_flip_vertically_on_write(0);

    const int stride = static_cast<int>(width) * 3;
    const bool written = stbi_write_png(
        path.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        stride
    ) != 0;

    stbi_write_png_compression_level = level;
    stbi_write_force_png_filter      = filter;
    stbi_flip_vertically_on_write(flip);
    return written;
}

} // namespace Vkm::Engine
