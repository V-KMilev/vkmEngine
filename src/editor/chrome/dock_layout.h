#pragma once

namespace Vkm::Engine {

// Dockable window names. ImGui's ini keys a window by name, so two spellings
// would be two windows.
inline constexpr const char* HIERARCHY_WINDOW = "Hierarchy";
inline constexpr const char* VIEWPORT_WINDOW  = "Viewport";
inline constexpr const char* INSPECTOR_WINDOW = "Inspector";
inline constexpr const char* MATERIAL_WINDOW  = "Material";
inline constexpr const char* ASSETS_WINDOW    = "Assets";
inline constexpr const char* ANIMATION_WINDOW = "Animation";
inline constexpr const char* ERRORS_WINDOW    = "Errors";
inline constexpr const char* BUILD_WINDOW     = "Build";

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
 * @brief Dock the next window begun beside @p beside the first time it shows.
 *
 * For a window a saved layout predates, which it would otherwise show floating.
 *
 * @param beside A window whose dock node it joins.
 */
void dockNextBeside(const char* beside);

} // namespace Vkm::Engine
