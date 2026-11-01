#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "ecs/prefab_instances.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/prefab/prefab_instance.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Entity subtrees saved once and instanced many times.
 *
 * A prefab is a scene fragment, one entity and its descendants, in its own file.
 * Instances are built fresh on each scene load, so editing the prefab changes them
 * all. A scene stores an instance as a reference, the root's Transform and its
 * overrides.
 *
 * Referenced by path, not through the AssetLibrary: nothing cooks them. Each file
 * carries a scene's assets block, so it can be instantiated into a scene that
 * never held its meshes. Every entry point reports an unusable document and
 * returns rather than throwing. Overrides: docs/reference/io.md, "Per-instance
 * overrides".
 */
namespace Prefab {

    /**
     * @brief Write @p root and its descendants to @p path as a prefab.
     *
     * The root's Transform is saved as the authored pose; an instance replaces it.
     * @p path is stored on the root verbatim, so a project-relative one stays
     * portable. The subtree becomes an instance of the file, its overrides dropped.
     *
     * @param scene     Scene holding the subtree.
     * @param root      Entity whose subtree becomes the prefab.
     * @param path      Destination file, project-relative or absolute.
     * @param resources Resolves asset handles to names.
     * @return True on success; false if the entity is dead, the subtree touches
     *         another instance, or the write fails.
     */
    bool save(Scene& scene, EntityId root, const std::string& path, const ResourceManager& resources);

    /**
     * @brief Does the prefab at @p path define @p component on the entity
     *        @p uid names?
     *
     * Answers whether an override addressing that pair could ever apply. Parses
     * the file again only when it has changed since the last read.
     *
     * @param path      Prefab file to read.
     * @param uid       Entity identity inside the prefab.
     * @param component Component key, as SceneSerializer writes it.
     * @return True when the prefab holds that entity and that component on it.
     */
    bool definesComponent(const std::string& path, uint32_t uid, const std::string& component);

    /**
     * @brief Instantiate @p path into @p scene, placing the root at @p at.
     *
     * @param scene     Scene to build into.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab file to read.
     * @param at        Pose for the instance root; the prefab's authored
     *                  Transform is replaced by it.
     * @return The instance root, or a default (invalid) EntityId on failure.
     */
    EntityId instantiate(
        Scene& scene,
        ResourceManager& resources,
        const std::string& path,
        const Transform& at
    );

    /**
     * @brief Instantiate at the prefab's own authored pose.
     *
     * The root is marked as a @ref PrefabInstance of @p path, so the scene stores
     * a reference to the file rather than the expanded entities.
     *
     * @param scene     Scene to build into.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab file to read, project-relative or absolute.
     * @return The instance root, or a default (invalid) EntityId on failure.
     */
    EntityId instantiate(Scene& scene, ResourceManager& resources, const std::string& path);

    /**
     * @brief Build a prefab into an entity that already exists.
     *
     * For a caller restoring entities at their saved slots: a root the prefab
     * allocated would take a slot another entity is waiting for. @p root receives
     * the prefab root's components and children, and keeps its own Transform.
     *
     * The caller marks @p root as a @ref PrefabInstance, so the overrides sit on
     * it before the build; unmarked, the next save writes the result inline.
     *
     * @param scene     Scene to build into.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab file to read.
     * @param root      Existing entity to become the instance root.
     * @param overrides Per-instance field deltas, addressed by PrefabEntity uid.
     * @param drift     Optional: one message per override the prefab no longer
     *                  has a home for; the override is kept.
     * @param slots     Optional: an earlier build's slots; an entity whose slot is
     *                  still free is built back into it.
     * @return True if the prefab was read and built.
     */
    bool instantiateInto(
        Scene& scene,
        ResourceManager& resources,
        const std::string& path,
        EntityId root,
        const std::vector<PrefabOverride>& overrides = {},
        std::set<std::string>* drift = nullptr,
        const BuiltSlots* slots = nullptr
    );

    /**
     * @brief Re-read one component of one instance entity from the prefab.
     *
     * An instance's component is the prefab's value patched by its overrides, so
     * dropping an override is a re-read, not an undo. Only @p component is
     * touched. Asks the file system per call, so not for a per-frame path.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab the instance was built from.
     * @param entity    Entity receiving the component.
     * @param uid       That entity's identity inside the prefab.
     * @param component Component key, as SceneSerializer writes it.
     * @param overrides Every override on the instance; only this uid's apply.
     * @return True when the prefab defines the component and it was loaded;
     *         false for the root's Transform, which is the instance's own pose.
     */
    bool reloadComponent(
        Scene& scene,
        ResourceManager& resources,
        const std::string& path,
        EntityId entity,
        uint32_t uid,
        const std::string& component,
        const std::vector<PrefabOverride>& overrides
    );

} // namespace Prefab

} // namespace Vkm::Engine
