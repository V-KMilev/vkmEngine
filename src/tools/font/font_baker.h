#pragma once

#include <string>

#include "resource/asset/font_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Bake a TrueType font into an SDF FontAsset registered in @p resources.
 *
 * Renders each printable-ASCII glyph to a signed distance field and packs them
 * into one atlas held INSIDE the FontAsset, which is self-contained - no
 * separate TextureAsset. The SDF is what lets one bake stay crisp at any size.
 *
 * @param resources   Manager that takes ownership of the FontAsset.
 * @param ttfPath     Absolute path to the .ttf file to read.
 * @param name        Asset name for the FontAsset (its findByName key).
 * @param pixelHeight Baked glyph height; 64 is a good default for UI text.
 * @return Handle to the baked FontAsset, or an invalid handle on failure.
 */
Handle<FontAsset> bakeFontSDF(
    ResourceManager& resources,
    const std::string& ttfPath,
    const std::string& name,
    float pixelHeight = 64.0f
);

} // namespace Engine
