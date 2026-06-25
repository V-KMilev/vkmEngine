#pragma once

#include <nlohmann/json.hpp>

#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "ecs/component/collider.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/physics_world.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "system/script/script_component.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * Each component type has a `save` and `load` overload. Add a new component
 * by adding a pair here. Asset handles (in Mesh) are resolved by
 * stable name through ResourceManager::findByName; entity references
 * (Hierarchy::parent) are stored as the saved scene-table index, which
 * resolves directly because SceneSerializer recreates each entity at its
 * saved slot.
 *
 * Animation serializes in full (all three tracks + the per-track easing by
 * stable name); see save/load(Animation) below.
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

    /**
     * @brief Rigidbody: dynamics + material fields. inverseMass / invInertiaLocal
     * are derived from mass + Collider on load, and the runtime sleep state
     * (sleeping / sleepTimer) is not persisted.
     */
    nlohmann::json save(const Rigidbody&);
    void load(const nlohmann::json&, Rigidbody&);

    nlohmann::json save(const Collider&);
    void load(const nlohmann::json&, Collider&);

    /**
     * @brief PhysicsWorld: the scene's singleton physics settings (gravity + solver
     * iteration count). Plain JSON primitives; load tolerates missing keys.
     */
    nlohmann::json save(const PhysicsWorld&);
    void load(const nlohmann::json&, PhysicsWorld&);

    nlohmann::json save(const Mesh&, const ResourceManager&);
    void load(const nlohmann::json&, Mesh&, const ResourceManager&);

    /**
     * @brief Hierarchy: only `parent` is serialized; sibling pointers are rebuilt
     * on load by re-running HierarchyOperations::setParent. The returned
     * JSON stores the parent's *old-file* entity index.
     */
    nlohmann::json save(const Hierarchy&);
    /** @brief Returns the parent's old-file index (uint32_t max if root). */
    uint32_t loadParentIndex(const nlohmann::json&);

    /**
     * @brief Animation: serializes all three tracks (position/rotation/scale),
     * playback state, and the per-track easing function by stable name.
     */
    nlohmann::json save(const Animation&);
    void load(const nlohmann::json&, Animation&);

    /**
     * @brief ScriptComponent: each behavior is stored by its registered type name
     * (BehaviorRegistry key) and recreated through the registry on load. Each
     * behavior's tunable fields are persisted via Behavior::visitFields (a
     * `properties` object per behavior); load drops any behavior whose type
     * is not registered, and keeps a field's default when its key is absent.
     */
    nlohmann::json save(const ScriptComponent&);
    void load(const nlohmann::json&, ScriptComponent&);

} // namespace ComponentSerializer

} // namespace Engine
