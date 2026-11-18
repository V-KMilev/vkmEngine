#pragma once

#include <string>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

namespace EditorActions {

inline constexpr const char* PREFAB_EXTENSION = ".json";  ///< A prefab file's extension: what listPrefabs()
                                                          ///< and the prefab file pickers look for.

/**
 * @brief The project's prefabs by project-relative path, as an instance stores them, sorted.
 *
 * A folder under Roots::prefabs() that cannot be read ends the walk, not the editor.
 *
 * @return The paths; empty for none.
 */
std::vector<std::string> listPrefabs();

/**
 * @brief Write @p entity's subtree to prefabs/ and make @p entity an instance of that file.
 *
 * A second save writes over the same file. The assets it names are cooked first, as a scene's save
 * does, or the names would resolve to nothing once the session that imported them ends.
 *
 * @param scene     Scene holding the subtree.
 * @param resources Resolves asset handles to names, and is cooked.
 * @param state     For the toast and the dirty flag.
 * @param entity    Root of the subtree to save.
 * @return True when the prefab was written.
 */
bool saveAsPrefab(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity);

/**
 * @brief Make the instance @p root an ordinary subtree, overrides and all, as one undo step.
 *
 * @param scene The scene holding it.
 * @param state Receives the step.
 * @param root  An instance root.
 */
void unpackPrefab(Scene& scene, EditorState& state, EntityId root);

/**
 * @brief Build and select an instance of the prefab at @p path where the view looks (placeInView).
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the step, the selection and the dirty flag.
 * @param path      Prefab file, project-relative or absolute.
 * @return The instance root, or an invalid EntityId when the prefab could not be read.
 */
EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path);

/**
 * @brief The effects the engine ships: the prefabs of its effects pack (ContentPack::shipped).
 *
 * @return Each one's prefab file name, in name order.
 */
std::vector<std::string> shippedEffects();

/**
 * @brief Place a shipped effect as placePrefab does, first copying in the pack files the project lacks.
 *
 * Never copies over a file the project has: the project owns its copy, edits and all.
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the step, the selection, the dirty flag and the toasts.
 * @param file      The effect's prefab file name, as shippedEffects() gives it.
 * @return The instance root, or an invalid EntityId when the pack could not be copied or read.
 */
EntityId placeEffect(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& file);

} // namespace EditorActions

} // namespace Vkm::Engine
