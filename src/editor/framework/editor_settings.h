#pragma once

#include <string>

namespace Vkm::Engine {

struct EditorState;
struct RenderSettings;

/**
 * @brief Persistent editor settings (panel widths, toggles, snap config, keybinds).
 *
 * Saves and loads a slim JSON document in the project root so the editor
 * comes up the same way it shut down. ImGui's own ini covers floating-window
 * positions and table column widths; this file covers EditorState fields that
 * the engine owns (panel sizes, gizmo defaults, snap step sizes, key
 * rebindings, recent scenes).
 *
 * The file lives in the open project's root (see path()); the recent-projects
 * list is the one exception, kept at the engine root so it survives switching
 * projects. Failures are non-fatal: load() returns false on missing/invalid
 * files and the editor starts with built-in defaults.
 */
namespace EditorSettings {

/**
 * @brief Load persisted settings into state.
 *
 * @param state  Editor state to populate with the saved fields; left at its
 *               built-in defaults when the file is missing or invalid.
 * @param render Render settings to populate from the file's renderSettings
 *               block (machine-quality tuning: MSAA, shadow resolution,
 *               effect toggles/params - it does not serialize with scenes).
 * @return false on a missing or invalid settings file, true on success.
 */
bool load(EditorState& state, RenderSettings& render);
/**
 * @brief Write the editor-owned settings to the settings file.
 *
 * @param state  Editor state whose persistent fields are serialized.
 * @param render Render settings written to the file's renderSettings block.
 * @return false on write failure, true on success.
 */
bool save(const EditorState& state, const RenderSettings& render);

/**
 * @brief Filesystem path the loader and saver operate on.
 *
 * Recomposed on each call from the open project's root, so it follows the
 * editor when it switches projects.
 *
 * @return Absolute path to the settings JSON document.
 */
std::string path();

}  // namespace EditorSettings

}  // namespace Vkm::Engine
