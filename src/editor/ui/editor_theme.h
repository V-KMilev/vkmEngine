#pragma once

namespace Vkm::Engine {

/**
 * @brief Apply the editor's dark theme (colors, rounding, spacing) to the
 *        current ImGui context.
 *
 * Every metric in the theme is authored against the 15 px reference font, the
 * same reference EditorStyle::px() uses, and scaled by @p scale on the way in.
 *
 * @param scale The window's content scale - whatever the editor font was sized
 *              by. Passing 1.0f leaves the theme at its reference metrics,
 *              which on a HiDPI display puts 2x text in 1x chrome.
 */
void applyEditorTheme(float scale = 1.0f);

} // namespace Vkm::Engine
