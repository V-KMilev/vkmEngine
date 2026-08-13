#pragma once

#include <string>

#include "ecs/entity.h"
#include "ecs/component/transform.h"

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief Entity subtrees saved once and instanced many times.
 *
 * A prefab is a scene fragment: the same per-entity component shape a scene
 * uses, for one entity and its descendants, in its own file. Instancing one
 * builds those entities fresh, so editing the prefab changes every instance the
 * next time a scene is loaded - which is the whole point, and the thing
 * duplicating entity blocks into a scene cannot do.
 *
 * A scene stores an instance as a reference plus the root's Transform, not as
 * the expanded entities. That keeps the scene file small, and keeps the prefab
 * the single definition of what the thing is.
 *
 * **Only the root Transform varies per instance.** Anything else that differs -
 * a different colour, a different mesh - is a different prefab. Per-field
 * overrides are the obvious extension, and the format leaves room for them (an
 * `overrides` object beside the transform), but they need a way to name a field
 * on a specific entity inside the subtree and a policy for what happens when the
 * prefab's shape changes underneath an override. That is the expensive half of
 * the design and it is deliberately not guessed at here.
 *
 * Prefabs are referenced by path rather than through the AssetLibrary because
 * nothing cooks them - they are authored JSON, read as-is, like scenes.
 */
namespace Prefab {

    /**
     * @brief Write @p root and its descendants to @p path as a prefab.
     *
     * The root's own Transform is saved with it as the prefab's authored pose;
     * an instance replaces it. Parent links are stored as indices within the
     * file, so the subtree survives independently of the entity ids it had.
     *
     * @param scene     Scene holding the subtree.
     * @param root      Entity whose subtree becomes the prefab.
     * @param path      Destination file path.
     * @param resources Resolves asset handles to names.
     * @return True on success; false if the entity is dead or the write fails.
     */
    bool save(const Scene& scene, EntityId root, const std::string& path,
              const ResourceManager& resources);

    /**
     * @brief Instantiate @p path into @p scene, placing the root at @p at.
     *
     * @param scene     Scene to build into.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab file to read.
     * @param at        Pose for the instance root; the prefab's authored
     *                  Transform is replaced by it.
     * @return The instance root, or a default (invalid) Entity on failure.
     */
    Entity instantiate(Scene& scene, ResourceManager& resources,
                       const std::string& path, const Transform& at);

    /**
     * @brief Instantiate at the prefab's own authored pose.
     */
    Entity instantiate(Scene& scene, ResourceManager& resources, const std::string& path);

    /**
     * @brief Build a prefab into an entity that already exists.
     *
     * The scene loader restores entities at their saved slot, so it must create
     * the instance root itself and fill it afterwards; letting the prefab
     * allocate the root would take a slot another entity is waiting for.
     *
     * @p root receives the prefab root's components and children. Its Transform
     * is left alone - the instance's pose belongs to whoever placed it, not to
     * the prefab.
     *
     * @param scene     Scene to build into.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab file to read.
     * @param root      Existing entity to become the instance root.
     * @return True if the prefab was read and built.
     */
    bool instantiateInto(Scene& scene, ResourceManager& resources,
                         const std::string& path, Entity root);

} // namespace Prefab

} // namespace Engine
