#pragma once

#include <string>

#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Bake a TrueType font into an SDF FontAsset registered in @p resources.
 *
 * Renders every glyph FontAsset covers to a signed distance field, packs them
 * into one atlas held INSIDE the FontAsset, which is self-contained - no
 * separate TextureAsset - and records the face's kerning between them. The SDF
 * is what lets one bake stay clean across a range of sizes - to roughly three
 * times the height it was baked at. The atlas is sized from the glyphs it has
 * to hold, with a gutter between them wide enough for FontAsset::MIP_LEVELS.
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

} // namespace Vkm::Engine
