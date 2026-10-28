#include "debug/screenshot.h"

// A private copy of the writer, so its settings are this file's alone; its initialisers trip -Wextra.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_STATIC
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

    stbi_write_png_compression_level = 1;
    stbi_write_force_png_filter      = 0;

    const int stride = static_cast<int>(width) * 3;
    return stbi_write_png(
        path.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        stride
    ) != 0;
}

} // namespace Vkm::Engine
