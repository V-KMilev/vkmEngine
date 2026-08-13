#pragma once

#include <string>

#include "resource/asset/font_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Bake a TrueType font into an SDF FontAsset registered in @p resources.
 *
 * Loads the .ttf at @p ttfPath, renders each printable-ASCII glyph to a signed
 * distance field, packs them into one atlas held INSIDE the FontAsset (it is
 * self-contained - no separate TextureAsset), and stores the per-glyph
 * metrics on that FontAsset added under @p name. The SDF atlas is what lets one bake stay crisp at any text size.
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
