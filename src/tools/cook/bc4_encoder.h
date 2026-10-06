#pragma once

#include <cstdint>

namespace Vkm::Engine::AssetCooker {

/**
 * @brief Compress sixteen 8-bit values, a 4x4 block in row order, to one BC4 (RGTC1) block.
 *
 * Both palettes (eight values; six plus exact 0 and 255) are tried from the block's extremes,
 * refined by least squares for a few rounds; the smaller error is written. Integer throughout,
 * so the bytes are identical on every compiler and build type, which a test pins.
 *
 * @param values The sixteen values, row by row.
 * @param block  Receives the 8-byte block: two endpoints, then 48 bits of 3-bit indices.
 */
void encodeBC4Block(const uint8_t values[16], uint8_t block[8]);

/**
 * @brief Compress sixteen two-channel texels to one BC5 (RGTC2) block.
 *
 * Two BC4 blocks, red then green, interpolated independently: why it holds a normal's x and y
 * well.
 *
 * @param texels The sixteen texels, row by row, red and green interleaved.
 * @param block  Receives the 16-byte block.
 */
void encodeBC5Block(const uint8_t texels[32], uint8_t block[16]);

} // namespace Vkm::Engine::AssetCooker
