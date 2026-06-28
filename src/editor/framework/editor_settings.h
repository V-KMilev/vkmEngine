#pragma once

#include <string>

namespace Engine {

struct EditorState;

/**
 * @brief Persistent editor settings (panel widths, toggles, snap config, keybinds).
 *
 * Saves and loads a slim JSON document in the project root so the editor
 * comes up the same way it shut down. ImGui's own ini covers floating-window
 * positions and table column widths; this file covers EditorState fields that
 * the engine owns (panel sizes, gizmo defaults, snap step sizes, key
 * rebindings, recent scenes).
 *
 * Path defaults to `APP_ROOT_DIR/editor_settings.json`. Failures are non-fatal:
 * load() returns false on missing/invalid files and the editor starts with
 * built-in defaults.
 */
namespace EditorSettings {

/**
 * @brief Load persisted settings into state.
 *
 * @param state Editor state to populate with the saved fields; left at its
 *              built-in defaults when the file is missing or invalid.
 * @return false on a missing or invalid settings file, true on success.
 */
bool load(EditorState& state);
/**
 * @brief Write the editor-owned settings to the settings file.
 *
 * @param state Editor state whose persistent fields are serialized.
 * @return false on write failure, true on success.
 */
bool save(const EditorState& state);

/**
 * @brief Filesystem path the loader and saver operate on.
 *
 * Resolved once at startup, defaulting to APP_ROOT_DIR/editor_settings.json.
 *
 * @return Absolute path to the settings JSON document.
 */
std::string path();

}  // namespace EditorSettings

}  // namespace Engine
