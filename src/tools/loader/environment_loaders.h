#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

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

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Load a Radiance .hdr equirectangular image as linear float RGB.
 *
 * Uses stb_image's float path. Flipped vertically so v = 1 is "up", matching
 * the GL texture origin and the equirect bake shader. Returns an empty image
 * (valid() == false) on failure; the caller logs/handles it.
 */
HDRImage loadHDRImage(const std::string& filePath);

/**
 * @brief CPU-side 8-bit RGBA image (e.g. a color-grading LUT strip).
 *
 * Flipped vertically on load to match the GL texture origin. Empty pixels
 * means the load failed.
 */
struct LDRImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<unsigned char> pixels;  ///< width * height * 4, RGBA

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Load an 8-bit image as RGBA (used for the color-grading LUT strip).
 * Returns an empty image (valid() == false) on failure.
 */
LDRImage loadColorLUT(const std::string& filePath);

} // namespace Engine
