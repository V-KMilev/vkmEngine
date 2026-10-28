#pragma once

namespace Vkm::Engine {

// Window names. ImGui's ini keys a window by name, so two spellings would be
// two windows. Preferences floats; the rest dock.
inline constexpr const char* HIERARCHY_WINDOW   = "Hierarchy";
inline constexpr const char* VIEWPORT_WINDOW    = "Viewport";
inline constexpr const char* INSPECTOR_WINDOW   = "Inspector";
inline constexpr const char* TERRAIN_WINDOW     = "Terrain";
inline constexpr const char* ASSETS_WINDOW      = "Assets";
inline constexpr const char* ANIMATION_WINDOW   = "Animation";
/// The id alone: its title, before it, counts the errors (`Console (2)###Console`).
inline constexpr const char* CONSOLE_WINDOW     = "###Console";
inline constexpr const char* PREFERENCES_WINDOW = "Preferences";

// Default side sizes, in design pixels. The bottom row fits one row of
// AssetBrowserPanel tiles at their default size.
inline constexpr float HIERARCHY_WIDTH = 260.0f;
inline constexpr float INSPECTOR_WIDTH = 380.0f;
inline constexpr float BOTTOM_HEIGHT   = 250.0f;

/**
 * @brief Whether the dockspace has a layout to restore.
 *
 * Ask before the dockspace is submitted in the frame, while its layout can
 * still be built.
 *
 * @param dockspace ImGui id of the dockspace.
 * @return true when ImGui holds a node for it.
 */
bool hasLayout(unsigned int dockspace);

/**
 * @brief Lay the dockspace out the way a first launch sees it.
 *
 * Discards whatever the node held, so this is also Window > Reset Layout.
 * Call before the dockspace is submitted in the frame.
 *
 * @param dockspace ImGui id of the dockspace.
 * @param width     In screen pixels.
 * @param height    In screen pixels.
 */
void buildDefaultLayout(unsigned int dockspace, float width, float height);

/**
 * @brief Make the next window begun the viewport's: its node never shows a tab bar.
 *
 * Never, not hidden: ImGui marks a hidden tab bar with a triangle in the node's
 * corner, over the scene.
 */
void setViewportWindowClass();

} // namespace Vkm::Engine
