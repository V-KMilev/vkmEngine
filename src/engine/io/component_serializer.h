#pragma once

#include <nlohmann/json.hpp>

#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"

namespace Engine {

class ResourceManager;
struct EnvironmentConfig;  // system/render/render_view.h

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * Each component type has a `save` and `load` overload. Add a new component
 * by adding a pair here. Asset handles (in Mesh) are resolved through
 * ResourceManager::findByName; entity references (Hierarchy::parent) are
 * stored as the old-file entity index and remapped in SceneSerializer::load.
 *
 * Animation is intentionally not serialized in Phase 1 - keyframe track
 * (de)serialization is its own design decision.
 */
namespace ComponentSerializer {

    nlohmann::json save(const Name&);
    void load(const nlohmann::json&, Name&);

    nlohmann::json save(const Transform&);
    void load(const nlohmann::json&, Transform&);

    nlohmann::json save(const Camera&);
    void load(const nlohmann::json&, Camera&);

    nlohmann::json save(const Light&);
    void load(const nlohmann::json&, Light&);

    nlohmann::json save(const Mesh&, const ResourceManager&);
    void load(const nlohmann::json&, Mesh&, const ResourceManager&);

    /// Hierarchy: only `parent` is serialized; sibling pointers are rebuilt
    /// on load by re-running HierarchyOperations::setParent. The returned
    /// JSON stores the parent's *old-file* entity index.
    nlohmann::json save(const Hierarchy&);
    /// Returns the parent's old-file index (uint32_t max if root).
    uint32_t loadParentIndex(const nlohmann::json&);

    /// Animation: serializes all three tracks (position/rotation/scale),
    /// playback state, and the per-track easing function by stable name.
    nlohmann::json save(const Animation&);
    void load(const nlohmann::json&, Animation&);

    /// EnvironmentConfig: the singleton Environment entity's rendering/post
    /// stack. All fields are JSON primitives; load tolerates missing keys.
    nlohmann::json save(const EnvironmentConfig&);
    void load(const nlohmann::json&, EnvironmentConfig&);

} // namespace ComponentSerializer

} // namespace Engine
