#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief CPU-side equirectangular HDR image (linear RGB, 3 floats/texel).
 *
 * Row-major, bottom-up (GL's origin). Empty pixels means the load failed.
 */
struct HDRImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<float> pixels;  ///< width * height * 3

    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Load a Radiance .hdr equirectangular image as linear float RGB.
 *
 * Flipped so v = 1 is "up", as the GL origin and the equirect bake shader expect.
 *
 * @param filePath Path to the .hdr file.
 * @return The image, or one whose isValid() is false on a (logged) failure.
 */
HDRImage loadHDRImage(const std::string& filePath);

} // namespace Vkm::Engine
