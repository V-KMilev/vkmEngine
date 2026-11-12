#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneSession;

/**
 * @brief The editor's keyboard shortcuts: panel toggles, scene save and load, entity operations, gizmo modes.
 *
 * Not toggleEditor: process() sees no frame while the editor is hidden, so that bind sits with
 * EditorSystem's visibility gate.
 */
namespace EditorShortcuts {

/**
 * @brief Act on this frame's shortcuts; the caller asks InputOwnership::editorHasKeys first.
 *
 * @param ec Editor context holding the scene, state and keybinds to read and mutate.
 */
void process(EditorContext& ec);

} // namespace EditorShortcuts

} // namespace Vkm::Engine
