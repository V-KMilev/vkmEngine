#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Decoded 8-bit RGBA pixels, and nothing else.
 *
 * Not an asset: the splash runs before a project's assets load. Rows run bottom-up, GL's order.
 */
struct DecodedImage {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> pixels;  ///< width * height * 4, tightly packed.

    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Decode an image file to 8-bit RGBA.
 *
 * Any channel count is expanded to four. A failure is logged, not thrown: a host must still
 * start without these pixels.
 *
 * @param filePath Path to a PNG, JPG, TGA or BMP, resolved against the project.
 * @return The decoded pixels, or an image whose isValid() is false.
 */
DecodedImage decodeImageRGBA(const std::string& filePath);

/**
 * @brief Make stb decode bottom-up on the calling thread, GL's row order.
 *
 * Call before each such decode. Per thread, not stb's process-wide flag: the window icon decodes
 * the other way round (see WindowManager), and a decode may run on any thread.
 */
void decodeImagesBottomUp();

} // namespace Vkm::Engine
