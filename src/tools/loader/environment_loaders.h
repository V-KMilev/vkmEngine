#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief CPU-side equirectangular HDR image (linear RGB, 3 floats/texel).
 *
 * Row-major, bottom-up (flipped on load to match GL texture origin). Empty
 * pixels means the load failed.
 */
struct HDRImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<float> pixels;  ///< width * height * 3, linear RGB

    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Load a Radiance .hdr equirectangular image as linear float RGB.
 *
 * Uses stb_image's float path. Flipped vertically so v = 1 is "up", matching
 * the GL texture origin and the equirect bake shader.
 *
 * @param filePath Path to the Radiance .hdr file to load.
 * @return The decoded HDR image, or an empty image (isValid() == false) on
 *         failure; the caller logs/handles it.
 */
HDRImage loadHDRImage(const std::string& filePath);

} // namespace Vkm::Engine
