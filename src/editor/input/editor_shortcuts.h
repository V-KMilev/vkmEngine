#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief Keyboard-shortcut dispatcher for the editor.
 *
 * Translates the configured keybinds into editor commands: panel toggles, scene
 * save/load (via the controller), entity ops (delete/duplicate/focus/deselect)
 * and gizmo mode changes. Reads keybinds and mutates EditorState.
 *
 * The toggleEditor bind, which shows the hidden editor again, is not here:
 * process() runs past EditorSystem's visibility gate, so it never sees a frame
 * while the editor is hidden. The toggle sits with the gate.
 */
namespace EditorShortcuts {

/**
 * @brief Process this frame's shortcuts.
 *
 * The caller asks InputOwnership::editorHasKeys first.
 *
 * @param ec Editor context holding the scene, state and keybinds to read and mutate.
 * @param sceneIO Controller the save/load shortcuts forward their intent to.
 */
void process(EditorContext& ec, SceneIOController& sceneIO);

} // namespace EditorShortcuts

} // namespace Vkm::Engine
