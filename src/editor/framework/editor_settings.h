#pragma once

#include <string>

namespace Engine {

struct EditorState;

/**
 * @brief Persistent editor settings (panel widths, toggles, snap config, keybinds).
 *
 * Saves and loads a slim JSON document next to the executable so the editor
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

bool load(EditorState& state);
bool save(const EditorState& state);

/** @brief Path the loader/saver uses. Resolved once at startup. */
std::string path();

}  // namespace EditorSettings

}  // namespace Engine
