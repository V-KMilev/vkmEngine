#pragma once

namespace Vkm::Engine {

// ImGui's ini keys a window by name, so two spellings would be two windows. Preferences floats.
inline constexpr const char* HIERARCHY_WINDOW   = "Hierarchy";
inline constexpr const char* VIEWPORT_WINDOW    = "Viewport";
inline constexpr const char* INSPECTOR_WINDOW   = "Inspector";
inline constexpr const char* TERRAIN_WINDOW     = "Terrain";
inline constexpr const char* ASSETS_WINDOW      = "Assets";
inline constexpr const char* ANIMATION_WINDOW   = "Animation";
inline constexpr const char* CONSOLE_WINDOW     = "###Console";  ///< The id alone: its title, before it,
                                                                 ///< counts the errors (`Console
                                                                 ///< (2)###Console`).
inline constexpr const char* PREFERENCES_WINDOW = "Preferences";

} // namespace Vkm::Engine
