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
 * A normal map (RG8) is filtered as a direction and renormalised. A metallic-roughness map
 * given its paired normal map has each level's roughness (G) raised by the normal detail its
 * texels average away (Toksvig, as von Mises-Fisher), so a glossy bumpy surface at range is as
 * rough as its bumps make it instead of sparkling.
 *
 * R8 becomes BC4, RG8 BC5, RGB8/RGBA8 and sRGB BC7. Uncompressed: smaller than a block,
 * TextureFilterOverride::Nearest, or not 8-bit (which also keeps one level, mipped at upload).
 * Encodes on the ThreadPool and returns when done.
 *
 * @param source A decoded texture: one level of tightly packed 8-bit texels;
 *               anything else passes through unchanged.
 * @param out    Receives the cooked params and pixels; name, recipe and path untouched.
 * @param roughnessNormal The normal map paired with a metallic-roughness @p source, decoded;
 *               null for none.
 * @return False when the source's pixels do not match its params or a level
 *         could not be filtered; @p out is then unspecified.
 */
[[nodiscard]] bool bakeTexture(
    const TextureAsset& source,
    TextureAsset& out,
    const TextureAsset* roughnessNormal = nullptr
);

} // namespace Vkm::Engine::AssetCooker
