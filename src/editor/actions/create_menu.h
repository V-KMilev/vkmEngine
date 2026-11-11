#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;
struct EditorState;

/// The Create menu: the built-in kinds by their rows (EditorActions::entityKinds), effects and prefabs.
namespace CreateMenu {

/**
 * @brief Draw the "Create" submenu, or "Create Child" under a parent, and create and select the choice.
 *
 * "Import Model" only asks for Ask::ImportModel: its modal is drawn outside the menu, which closes on
 * click (ModelImportDialog::draw).
 *
 * @param scene     Where new entities are created.
 * @param resources Passed through to EditorActions::createEntity.
 * @param state     Takes the new selection or the import request.
 * @param parent    Puts what is made under this entity, or {} for none.
 */
void draw(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent = {});

/**
 * @brief The Create menu's rows alone, for a popup that is nothing but them: the Hierarchy's "+" and
 *        its right click on empty space, where a lone "Create" submenu would be a step for nothing.
 *
 * @param scene     Where new entities are created.
 * @param resources Passed through to EditorActions::createEntity.
 * @param state     Takes the new selection or the import request.
 * @param parent    Puts what is made under this entity, or {} for none.
 */
void items(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent = {});

} // namespace CreateMenu

} // namespace Vkm::Engine
