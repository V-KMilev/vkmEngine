#pragma once

#include <memory>
#include <string>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"

#include "undo/asset_commands.h"
#include "editor_state.h"

namespace Vkm::Engine {

class Scene;

namespace EditorActions {

/**
 * @brief Register a copy of @p base under the first free name from @p name: a material of its own
 *        for a new thing, so tuning one leaves the others.
 *
 * Free means neither loaded nor in the library: a save would write over a library material not
 * loaded.
 *
 * @param resources Where the copy is registered.
 * @param base      The material copied.
 * @param name      The name wanted; "name 2", "name 3", ... when it is taken.
 * @return The copy, or a null handle on registration failure.
 */
MaterialHandle copyMaterial(ResourceManager& resources, MaterialHandle base, const std::string& name);

/**
 * @brief Put a copy of @p material on @p entity's Mesh, if that draws it, and open the copy's window.
 *
 * Edits to the copy stop reaching whatever else draws @p material; in an instance the step records
 * an override.
 *
 * @param scene     Scene holding the entity.
 * @param resources Where the copy is registered.
 * @param state     Takes the step and opens the copy's window.
 * @param entity    The entity the copy is for.
 * @param material  The material forked.
 * @return The copy, or a null handle on registration failure.
 */
MaterialHandle forkMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    MaterialHandle material
);

/**
 * @brief Register a copy of generateDefaultMaterial's material under a free name, on no entity.
 *
 * Named "Material", "Material 2", ...; a copy, as the default is what "material:default" resolves to.
 *
 * @param resources Where the material is registered.
 * @param state Its dirty flag is raised.
 * @return The new handle, or a null handle on failure.
 */
MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state);

/**
 * @brief Register a flat terrain named for a free assets/terrain/ path, written at the next save or Play.
 *
 * @param resources Where the terrain is registered.
 * @return The new handle.
 */
TerrainHandle createNewTerrain(ResourceManager& resources);

/**
 * @brief Rename an asset as one undo step, toasting the name it got when @p to was taken.
 *
 * The step records the name ResourceManager assigned, and the scene, which names the asset, is
 * marked unsaved.
 *
 * @tparam Asset Asset type being renamed.
 * @param resources Owns the asset and its name index.
 * @param state Takes the step and any toast.
 * @param handle Asset to rename.
 * @param from Name it had, kept for undo.
 * @param to Name the author typed.
 * @param label Undo-stack label, e.g. "Rename Material".
 */
template<typename Asset>
void renameAsset(
    ResourceManager& resources,
    EditorState& state,
    Handle<Asset> handle,
    const std::string& from,
    const std::string& to,
    const char* label
) {
    resources.rename(handle, to);

    const std::string assigned = resources.get(handle).name();
    if (assigned != to) {
        state.toasts.push(ToastKind::Info, "'" + to + "' was taken - renamed to '" + assigned + "'");
    }

    state.pushStep(
        std::make_unique<RenameAssetCommand<Handle<Asset>>>(resources, handle, from, assigned, label)
    );
}

} // namespace EditorActions

} // namespace Vkm::Engine
