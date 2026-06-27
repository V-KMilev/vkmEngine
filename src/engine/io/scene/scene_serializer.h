#pragma once

#include <string>

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief Save / load a Scene + the assets it references to/from JSON.
 *
 * Both entities and their referenced assets persist: SceneSerializer emits
 * an `assets` block (textures, materials, meshes) alongside the `entities`
 * block, and the loader uses AssetSerializer factories to recreate them on
 * the way back in. Asset references inside components (Mesh handles,
 * material texture refs) resolve by `name` through ResourceManager - the
 * stable identity across save/load.
 *
 * Load is transactional for BOTH entities and assets: entities deserialise
 * into a staging Scene and assets into a staging ResourceManager, and both
 * commit via swap only on full success - so a malformed file leaves the live
 * scene AND the live asset graph untouched (documented inline in the .cpp).
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
     * @brief Save @p scene + the assets it references to @p path.
     *
     * @return true on success; false (and a logged error) on I/O failure.
     */
    bool save(const Scene& scene, const ResourceManager& resources, const std::string& path);

    /**
     * @brief Load a scene from @p path, replacing the live @p scene atomically.
     *
     * Assets and entities both deserialise into staging containers (a staging
     * ResourceManager + a staging Scene) and commit via swap only on full
     * success - a failure leaves @p scene and the live asset graph unchanged,
     * with no orphaned assets (documented in the .cpp).
     *
     * @return true on success; false (and a logged error) on failure, with
     *         the live scene untouched.
     */
    bool load(Scene& scene, ResourceManager& resources, const std::string& path);

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
     *        saveToString(), replacing @p scene + @p resources atomically.
     *
     * Same transactional swap as load(): on failure both are left untouched.
     *
     * @return true on success; false (and a logged error) on failure.
     */
    bool loadFromString(const std::string& text, Scene& scene, ResourceManager& resources);

} // namespace SceneSerializer

} // namespace Engine
