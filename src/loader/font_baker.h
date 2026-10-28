#pragma once

#include <string>
#include <vector>

#include "io/project.h"
#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

class ResourceManager;

// The UI's fonts are baked at a height that scales with the display (a distance field magnifies
// well but not forever); the cap keeps the worst case a 2048-square atlas.
constexpr int   UI_FONT_REFERENCE_HEIGHT = 1080;
constexpr float UI_FONT_BASE_PIXELS      = 64.0f;
constexpr float UI_FONT_MAX_PIXELS       = 128.0f;

/**
 * @brief The height a UI font is baked at on a display @p displayHeight pixels tall.
 *
 * @param displayHeight The display's height in pixels; 0 or less when unknown.
 * @return Pixels; UI_FONT_BASE_PIXELS for an unknown display.
 */
float uiFontPixels(int displayHeight);

/**
 * @brief Bake a project's own fonts (Project::fonts) at @p pixelHeight.
 *
 * Any number of times: each is baked again in place of what its name held, so a font whose file
 * changed between two opens of a project is the new one. One that will not read is reported
 * (reportError) - the editor shows it in its Errors panel - and its text draws nothing, or the
 * last bake its name held. A name
 * the project no longer lists keeps what it held until the host closes.
 *
 * @param resources   Receives the fonts.
 * @param fonts       The project's list.
 * @param pixelHeight Baked glyph height: uiFontPixels for the display.
 */
void bakeProjectFonts(ResourceManager& resources, const std::vector<FontEntry>& fonts, float pixelHeight);

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
 * @param name        Asset name for the FontAsset (its findByName key); one already held is
 *                    baked again in place, its handle kept.
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
