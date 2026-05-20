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
 * Load is transactional at the scene level: entities deserialise into a
 * staging Scene first and are committed via Scene::swap only on full
 * success, so a malformed file leaves the live scene untouched. Asset
 * state is not transactional today (the asset graph is mutated before
 * the swap; documented inline in the .cpp).
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
     * @brief Emitted by callers of load() after a successful load.
     *
     * Subscribers (camera controllers, editor panels, gameplay code that
     * tracks entities) listen via EventSystem and refresh anything cached
     * across scene reloads. SceneSerializer itself does NOT publish -
     * that's the caller's responsibility, since only the caller has access
     * to an EventSystem.
     */
    struct SceneLoadedEvent {
        std::string path;
    };

    /**
     * @brief Save @p scene + the assets it references to @p path.
     *
     * @return true on success; false (and a logged error) on I/O failure.
     */
    bool save(const Scene& scene, const ResourceManager& resources, const std::string& path);

    /**
     * @brief Load a scene from @p path, replacing the live @p scene atomically.
     *
     * Assets are loaded first (idempotent: skips assets already present by
     * name) into the provided ResourceManager. Entities deserialise into a
     * staging Scene and only commit via swap on full success - a failure
     * leaves @p scene unchanged. Assets newly loaded along the way are
     * NOT rolled back on failure (the orphans persist until a manual
     * cleanup; documented in the .cpp).
     *
     * @return true on success; false (and a logged error) on failure, with
     *         the live scene untouched.
     */
    bool load(Scene& scene, ResourceManager& resources, const std::string& path);

} // namespace SceneSerializer

} // namespace Engine
