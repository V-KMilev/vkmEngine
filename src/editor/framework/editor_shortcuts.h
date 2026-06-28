#pragma once

namespace Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief Keyboard-shortcut dispatcher for the editor.
 *
 * Extracted from EditorSystem (god-file decomposition). Translates the
 * configured keybinds into editor commands: panel toggles, scene save/load
 * (via the controller), entity ops (delete/duplicate/focus/deselect) and
 * gizmo mode changes. Stateless; reads keybinds and mutates EditorState.
 *
 * The F5 "show the hidden editor again" toggle is deliberately NOT here:
 * it uses raw GLFW (ImGui is not processing input while the editor is
 * hidden) and stays part of EditorSystem's visibility gate.
 */
class EditorShortcuts {
    public:
        /**
         * @brief Process this frame's shortcuts.
         *
         * No-op while ImGui wants text input, so a keybind never fires while
         * the user is typing into a field.
         *
         * @param ec Editor context holding the scene, state and keybinds to read and mutate.
         * @param sceneIO Controller the save/load shortcuts forward their intent to.
         */
        void process(EditorContext& ec, SceneIOController& sceneIO);
};

} // namespace Engine
