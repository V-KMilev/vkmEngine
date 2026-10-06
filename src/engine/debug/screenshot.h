#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Write an 8-bit RGB image to @p path as a PNG.
 *
 * The cheapest PNG the encoder makes. Its process-wide settings are restored after.
 *
 * @param path   Where the PNG goes; the directory must exist.
 * @param width  Image width in texels.
 * @param height Image height in texels.
 * @param rgb    width * height * 3 bytes, top row first, rows unpadded.
 * @return Whether the file was written.
 */
bool writeScreenshotPng(
    const std::string& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgb
);

} // namespace Vkm::Engine
