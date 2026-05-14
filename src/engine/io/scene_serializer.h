#pragma once

#include <string>

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief Save / load a Scene's entities + components to/from a JSON file.
 *
 * Scope (Phase 1): entities and their component data are persisted. Assets
 * referenced by Mesh components are looked up by their `name` field in the
 * provided ResourceManager — assets themselves are not serialized. The
 * asset graph must already be populated when load() is called.
 *
 * Load semantics: the target Scene is cleared first, then entities are
 * re-created from the file. Saved entity indices do not survive across
 * runs (slot allocation differs); references between entities (e.g.
 * Hierarchy::parent) are remapped through a two-pass algorithm.
 */
namespace SceneSerializer {

    /// Emitted by callers of `load()` after a successful load. Subscribers
    /// (camera controllers, editor panels, gameplay code that tracks
    /// entities) listen via the EventSystem and refresh anything cached
    /// across scene reloads. SceneSerializer itself does NOT publish —
    /// that's the caller's responsibility, since only the caller has
    /// access to an EventSystem.
    ///
    /// Slot indices are preserved across save/load, so entity references
    /// held by file format consumers (Hierarchy::parent) work directly,
    /// and external holders just need to re-validate by index.
    struct SceneLoadedEvent {
        std::string path;
    };

    /// @return true on success; false (and a logged error) on I/O failure.
    bool save(const Scene& scene, const ResourceManager& resources, const std::string& path);

    /// Loads assets first (idempotent: skips assets already present by name),
    /// then clears the scene and rebuilds entities at their saved slot
    /// indices. ResourceManager is mutable because asset deserialization
    /// populates new assets.
    ///
    /// @return true on success; on failure the scene may be partially loaded.
    bool load(Scene& scene, ResourceManager& resources, const std::string& path);

} // namespace SceneSerializer

} // namespace Engine
