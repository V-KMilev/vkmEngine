#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Decoded 8-bit RGBA pixels, and nothing else.
 *
 * No handle, no name and no place in the ResourceManager: the splash runs
 * before a project's assets are loaded and outside any scene, so the image it
 * needs cannot be an asset.
 *
 * Rows run bottom-up, GL's own order, which is what every loader here asks stb
 * for. The flag that decides it is process-wide and stb reads it inside the
 * decode, so they all have to agree or a worker's texture comes out flipped.
 */
struct DecodedImage {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> pixels;  ///< width * height * 4, tightly packed RGBA.

    /// True when the decode produced pixels of a usable size.
    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/**
 * @brief Decode an image file to 8-bit RGBA.
 *
 * Whatever channel count the file has is expanded to four, so the caller has
 * one layout to handle. A failure is logged and returns an empty image rather
 * than throwing: what these pixels decorate is optional, and a host must still
 * start without them.
 *
 * @param filePath Path to a PNG, JPG, TGA or BMP, resolved against the project.
 * @return The decoded pixels, or an image whose valid() is false.
 */
DecodedImage decodeImageRGBA(const std::string& filePath);

} // namespace Vkm::Engine
