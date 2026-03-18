#pragma once

#include "ecs/entity.h"

namespace Engine {

class Scene;
class ResourceManager;
class CameraController;
struct FrameContext;
struct EditorState;

/**
 * @brief Entity operations invoked by the editor (menu bar, hierarchy, keybinds).
 *
 * Free functions that modify the Scene and update EditorState (selection, dirty flags).
 * Decoupled from any specific panel so both the menu bar and hierarchy can call them.
 */
namespace EditorActions {

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, const char* type);
void duplicateEntity(Scene& scene, EditorState& state, EntityId source);
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);
void focusOnSelected(FrameContext& ctx, EditorState& state, CameraController* camera);
void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state);

} // namespace EditorActions

} // namespace Engine
