#pragma once

#include <set>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/prefab_instance.h"
#include "ecs/component/transform.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Entity subtrees saved once and instanced many times.
 *
 * A prefab is a scene fragment: the same per-entity component shape a scene
 * uses, for one entity and its descendants, in its own file. Instancing one
 * builds those entities fresh, so editing the prefab changes every instance the
 * next time a scene is loaded.
 *
 * A scene stores an instance as a reference plus the root's Transform, not as
 * the expanded entities. What varies per instance is exactly that Transform and
 * the fields an override names; everything else belongs to the prefab.
 *
 * Prefabs are referenced by path rather than through the AssetLibrary because
 * nothing cooks them - they are authored JSON, read as-is, like scenes.
 *
 * A prefab carries the same **assets block** a scene does, for the subtree it
 * describes, and every entry point that reads components out of it loads that
 * block first. Component references are asset names, and a name resolves to
 * nothing unless something already loaded it, so without it a prefab was only
 * instantiable where something else happened to have loaded its meshes.
 *
 * Prefabs are hand-editable, so every entry point here reports a document it
 * cannot use and returns rather than throwing: the callers are an editor showing
 * a toast beside its file picker and a scene load with other entities to build.
 *
 * **Per-instance overrides.** A scene may store field deltas against an
 * instance, addressed by @ref PrefabEntity uid, component key and field key.
 * They are stored, never re-derived by diffing the built subtree against the
 * file: a load that could not resolve an asset name writes a different value
 * than it read, so a diff would manufacture overrides out of load failures, and
 * a missing prefab file would erase every override in the scene.
 *
 * **When the prefab changes underneath an override** the override is kept,
 * reported once, and not applied. The cases are: the uid is gone, the component
 * is gone, the field is gone, the value's type no longer matches, and the
 * root's Transform (which is the instance's own pose and could never have taken
 * effect). Keeping the entry means renaming a field and renaming it back does
 * not lose the user's edit.
 */
namespace Prefab {

    /**
     * @brief Is @p id inside (but not the root of) a prefab instance?
     *
     * Walks up rather than marking every descendant, so the prefab's own
     * entities carry no bookkeeping that could fall out of sync with their
     * root. The scene serializer asks this to decide which entities it may
     * describe, and @ref save to refuse a subtree that is not its own to give
     * away.
     *
     * @param scene Scene holding the entity.
     * @param id    Entity to test.
     * @return True when an ancestor of @p id carries PrefabInstance.
     */
    bool isInsideInstance(const Scene& scene, EntityId id);

    /**
     * @brief Write @p root and its descendants to @p path as a prefab.
     *
     * The root's own Transform is saved with it as the prefab's authored pose;
     * an instance replaces it. Parent links are stored as indices within the
     * file, so the subtree survives independently of the entity ids it had.
     *
     * @p path is resolved for the write and stored on the root verbatim, so a
     * project-relative one is what a scene carrying the instance goes on to
     * write - the form that still names the same file on another machine.
     *
     * The subtree becomes an instance of what it wrote, and its overrides are
     * dropped: the file now holds those values, so keeping them would pin the
     * instance to them and stop every later edit of the prefab from reaching it.
     *
     * @param scene     Scene holding the subtree.
     * @param root      Entity whose subtree becomes the prefab.
     * @param path      Destination file, project-relative or absolute.
     * @param resources Resolves asset handles to names.
     * @return True on success; false if the entity is dead, the subtree touches
     *         another instance, or the write fails.
     */
    bool save(Scene& scene, EntityId root, const std::string& path,
              const ResourceManager& resources);

    /**
     * @brief Does the prefab at @p path define @p component on the entity
     *        @p uid names?
     *
     * The prefab's own component block is the schema an override is checked
     * against, so this answers whether one addressing that pair could ever be
     * applied. Reads the file per call, like @ref reloadComponent.
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
    EntityId instantiate(Scene& scene, ResourceManager& resources,
                         const std::string& path, const Transform& at);

    /**
     * @brief Instantiate at the prefab's own authored pose.
     *
     * The root is created here and marked as a @ref PrefabInstance of @p path,
     * so the scene stores it as a reference to the file rather than as the
     * entities it expanded to.
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
     * @param overrides Per-instance field deltas, addressed by PrefabEntity uid.
     * @param drift     Optional: receives one message per override the prefab no
     *                  longer has a home for. The override itself is kept.
     * @return True if the prefab was read and built.
     */
    bool instantiateInto(Scene& scene, ResourceManager& resources,
                         const std::string& path, EntityId root,
                         const std::vector<PrefabOverride>& overrides = {},
                         std::set<std::string>* drift = nullptr);

    /**
     * @brief Re-read one component of one instance entity from the prefab.
     *
     * A component on an instance is the prefab's value patched by the
     * instance's overrides, so dropping an override is not an undo of the edit
     * that made it - it is a re-read of that definition, which is what this
     * does. Only @p component is touched; the rest of the entity is left alone.
     *
     * Reads the file per call, which suits the interactive edits it serves and
     * not a per-frame path.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset names to handles.
     * @param path      Prefab the instance was built from.
     * @param entity    Entity receiving the component.
     * @param uid       That entity's identity inside the prefab.
     * @param component Component key, as SceneSerializer writes it.
     * @param overrides Every override on the instance; only this uid's apply.
     * @return True when the prefab defines the component and it was loaded;
     *         false for the root's Transform, which is the instance's own pose
     *         and would be replaced by the prefab's authored one.
     */
    bool reloadComponent(Scene& scene, ResourceManager& resources, const std::string& path,
                         EntityId entity, uint32_t uid, const std::string& component,
                         const std::vector<PrefabOverride>& overrides);

} // namespace Prefab

} // namespace Vkm::Engine
