#pragma once

#include "base/container/storage_index.h"
#include "ecs/entity.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;

/**
 * @brief Whether an asset of @p type is given to @p entity: a material or mesh to a Mesh, a rig or clip
 *        to an Animator, a sound to an Audio Source, a terrain to a Terrain.
 *
 * @param type   The asset's kind; a texture is given to nothing.
 * @param scene  Holds the entity.
 * @param entity The entity asked about.
 * @return True when assignAsset gives it one.
 */
bool assignsTo(AssetType type, const Scene& scene, EntityId entity);

/**
 * @brief Give @p entity the asset @p key of @p type, through an EditScope: one undo step, and a prefab
 *        override on an instance. Nothing for a pairing assignsTo refuses.
 *
 * @param scene     Holds the entity.
 * @param resources Holds the asset.
 * @param state     Takes the step.
 * @param entity    The entity given the asset.
 * @param type      The asset's kind.
 * @param key       The asset.
 */
void assignAsset(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    AssetType type,
    StorageIndex key
);

/**
 * @brief Make the viewport's rect a drop target for an asset dragged from a tile.
 *
 * Call inside the viewport's window, after ec's viewport rect is set. An asset other than a mesh
 * is assigned to the entity under the cursor if that takes it; otherwise, if its kind makes
 * entities, it becomes a new one on the cursor's ray.
 *
 * @param ec The frame's editor context.
 */
void acceptViewportDrop(EditorContext& ec);

/**
 * @brief Inside another panel's drop target on @p entity's row, assign it the asset dragged.
 *
 * A material or mesh assigns to a Mesh, a sound to an Audio Source, a rig or clip to an
 * Animator, a terrain to a Terrain; any other pairing is not accepted.
 *
 * @param scene     Holds the entity.
 * @param resources Holds the asset.
 * @param state     Takes the step and the selection.
 * @param entity    The row's entity.
 */
void acceptAssetOn(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity);

/**
 * @brief In a drop target with no entity, make the asset dragged a new entity where the view looks.
 *
 * @param scene     Receives the entity.
 * @param resources Holds the asset.
 * @param state     Takes the step and the selection, and says where the view looks.
 */
void acceptAssetCreate(Scene& scene, ResourceManager& resources, EditorState& state);

} // namespace Vkm::Engine
