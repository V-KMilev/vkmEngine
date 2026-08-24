#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Save / load a Scene + the assets it references to/from JSON.
 *
 * Both entities and their referenced assets persist: an `assets` block
 * (textures, materials, meshes) sits alongside the `entities` block, and the
 * loader recreates them through the AssetSerializer factories. Asset references
 * inside components (Mesh handles, material texture refs) resolve by `name`
 * through ResourceManager - the stable identity across save/load.
 *
 * Load is transactional for BOTH entities and assets: entities deserialise
 * into a staging Scene and assets into a staging ResourceManager, and both
 * commit via swap only on full success - so a malformed file leaves the live
 * scene AND the live asset graph untouched.
 *
 * Slot indices survive a save -> load round trip - the loader recreates
 * each entity at the same slot via Scene::createEntityAt, so cross-entity
 * references in the file (e.g. Hierarchy::parent indices) work directly
 * without a remap step. Indices are NOT stable across live edits between
 * runs - editing the scene allocates fresh slots that may not match the
 * last saved layout.
 */
namespace SceneSerializer {
    /**
     * @brief Write every serialized component of @p id into @p out.
     *
     * Shared with the prefab writer, which stores the same per-entity shape - a
     * prefab is a scene fragment, and a second copy of this table would drift
     * from this one the first time a component is added.
     *
     * @param scene     Scene holding the entity.
     * @param id        Entity to serialize.
     * @param out       JSON object to fill, keyed by component name.
     * @param resources Resolves asset handles to their names.
     */
    void saveComponents(const Scene& scene, EntityId id, nlohmann::json& out,
                        const ResourceManager& resources);

    /**
     * @brief Add every component present in @p src to @p entity.
     *
     * Hierarchy is deliberately absent: a parent may not exist yet when its
     * child is read, so callers capture the link and wire it up in a second
     * pass once every entity exists.
     *
     * @param src       JSON object written by saveComponents.
     * @param scene     Scene to add into.
     * @param entity    The entity receiving the components.
     * @param resources Resolves asset names back to handles.
     */
    void loadComponents(const nlohmann::json& src, Scene& scene, EntityId entity,
                        const ResourceManager& resources);

    /**
     * @brief Save @p scene + the assets it references to @p path.
     *
     * @return true on success; false (and a logged error) on I/O failure.
     */
    bool save(const Scene& scene, const ResourceManager& resources, const std::string& path);

    /**
     * @brief Load a scene from @p path, replacing the live @p scene atomically.
     *
     * Assets and entities both deserialise into staging containers and commit
     * via swap only on full success, so a failure leaves @p scene and the live
     * asset graph unchanged, with no orphaned assets.
     *
     * @return true on success; false (and a logged error) on failure, with
     *         the live scene untouched.
     */
    bool load(Scene& scene, ResourceManager& resources, const std::string& path);

    /**
     * @brief Drop from @p id's unresolved-reference record every entry whose
     *        field has since been filled, and the record itself once it is empty.
     *
     * The record is what the load could not resolve, kept so a save writes the
     * names back rather than "" over them. A field the author has since filled
     * is no longer one of those: the save already leaves such a slot alone, but
     * the record went on saying the reference did not load, and the assets block
     * went on declaring a name the scene has stopped using - so the Inspector
     * told the author a mesh they had just chosen was still missing, and every
     * later load went looking for one that is not there.
     *
     * Answered by writing the entity and reading the field back, which is the
     * same question the save asks and therefore cannot drift from it. Idempotent
     * and cheap to call on an entity that has no record, which is every healthy
     * one.
     *
     * @param scene Scene holding the entity.
     * @param resources Asset graph the entity's references resolve against.
     * @param id Entity to prune; a dead one is ignored.
     */
    void pruneResolvedRefs(Scene& scene, const ResourceManager& resources, EntityId id);

    /**
     * @brief Serialize @p scene + the assets it references to an in-memory JSON
     *        string (same content as save(), no file written).
     *
     * The editor uses this for the play-mode snapshot, which must stay in
     * memory so pressing Stop can restore the authored scene without touching
     * disk. Pairs with loadFromString().
     *
     * @return The serialized document, or an empty string on failure.
     */
    std::string saveToString(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief Load a scene from an in-memory JSON string produced by
     *        saveToString(), replacing @p scene and merging into @p resources.
     *
     * The scene commits by the same transactional swap load() uses: on failure
     * it is left untouched. The asset graph is *not* swapped, which is the one
     * way this differs from load(). The document was written out of that graph
     * moments earlier, so it asks for nothing the graph does not already hold
     * and nothing is created - and every handle issued out of it before the
     * call still means what it meant. The editor's undo history is why that
     * matters: Stop keeps the steps taken before Play, each of which holds the
     * asset it is to put back, and a swap would leave those addressing a
     * manager that no longer exists.
     *
     * @param text A document from saveToString().
     * @param scene Scene to replace on success.
     * @param resources Asset graph to resolve against, added to only where a
     *        name is genuinely absent.
     * @return true on success; false (and a logged error) on failure.
     */
    bool loadFromString(const std::string& text, Scene& scene, ResourceManager& resources);

} // namespace SceneSerializer

} // namespace Vkm::Engine
