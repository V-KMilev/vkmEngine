#pragma once

namespace Vkm::Engine {

/**
 * @brief Apply the editor's dark theme (colors, rounding, spacing) to the current ImGui context.
 *
 * Metrics are authored against EditorStyle::REFERENCE_FONT_SIZE, as EditorStyle::px() is.
 *
 * @param scale Display content scale times the user's UI Scale; 1.0f on HiDPI puts 2x text
 *              in 1x chrome.
 */
void applyEditorTheme(float scale);

} // namespace Vkm::Engine
