#pragma once

#include "resource/asset/texture_asset.h"

namespace Vkm::Engine::AssetCooker {

/**
 * @brief Build the form a texture is cooked in: its whole mip chain, each level
 *        block-compressed where the content allows.
 *
 * Each level is filtered from the one above in linear floats (sRGB decoded, alpha-weighted so
 * transparent colour does not bleed), wrap mode as the edge rule, rounded to bytes once. A
 * colour texture with alpha keeps its coverage above 0.5 per level, so a cut-out does not thin.
 * A normal map (RG8) is filtered as a direction and renormalised.
 *
 * R8 becomes BC4, RG8 BC5, RGB8/RGBA8 and sRGB BC7. Uncompressed: smaller than a block,
 * TextureFilterOverride::Nearest, or not 8-bit (which also keeps one level, mipped at upload).
 * Encodes on the ThreadPool and returns when done.
 *
 * @param source A decoded texture: one level of tightly packed 8-bit texels;
 *               anything else passes through unchanged.
 * @param out    Receives the cooked params and pixels; name, recipe and path untouched.
 * @return False when the source's pixels do not match its params or a level
 *         could not be filtered; @p out is then unspecified.
 */
[[nodiscard]] bool bakeTexture(const TextureAsset& source, TextureAsset& out);

} // namespace Vkm::Engine::AssetCooker
