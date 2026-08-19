#pragma once

#include <nlohmann/json.hpp>

#include "ecs/environment.h"
#include "ecs/component/animation.h"
#include "ecs/component/animator.h"
#include "ecs/component/camera.h"
#include "ecs/component/collider.h"
#include "ecs/component/decal.h"
#include "ecs/component/lod.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "ecs/component/ui_button.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * Each component type has a `save` and `load` overload. Add a new component
 * by adding a pair here. Asset handles (Mesh, Decal) are resolved by
 * stable name through ResourceManager::findByName; entity references
 * (Hierarchy::parent) are stored as the saved scene-table index, which
 * resolves directly because SceneSerializer recreates each entity at its
 * saved slot.
 */
namespace ComponentSerializer {

    nlohmann::json save(const PhysicsSettings&);
    void load(const nlohmann::json&, PhysicsSettings&);

    nlohmann::json save(const Name&);
    void load(const nlohmann::json&, Name&);

    /**
     * @brief The scene-global Environment (lighting + fog + physics settings).
     *
     * Fully reflected: the field list lives once in environment.h and both
     * directions walk it, so adding an Environment field never touches the
     * serializers again. Missing keys keep the current values.
     */
    nlohmann::json save(const Environment&);
    void load(const nlohmann::json&, Environment&);

    nlohmann::json save(const Transform&);
    void load(const nlohmann::json&, Transform&);

    nlohmann::json save(const Camera&);
    void load(const nlohmann::json&, Camera&);

    nlohmann::json save(const Light&);
    void load(const nlohmann::json&, Light&);

    /**
     * @brief Rigidbody: dynamics + material fields. The runtime sleep state
     * (sleeping / sleepTimer) is not persisted.
     */
    nlohmann::json save(const Rigidbody&);
    void load(const nlohmann::json&, Rigidbody&);

    nlohmann::json save(const Collider&);
    void load(const nlohmann::json&, Collider&);

    nlohmann::json save(const Mesh&, const ResourceManager&);
    void load(const nlohmann::json&, Mesh&, const ResourceManager&);

    /**
     * @brief Animator: the rig, the clip and where playback stands.
     *
     * Blend state is deliberately absent and stays absent. A crossfade is a
     * second clip and a countdown, and a scene row holding that shape would
     * outlive the blend system that wrote it in a project with no migration
     * path; the six fields here are what any future blend system still needs.
     */
    nlohmann::json save(const Animator&, const ResourceManager&);
    void load(const nlohmann::json&, Animator&, const ResourceManager&);

    nlohmann::json save(const LOD&, const ResourceManager&);
    void load(const nlohmann::json&, LOD&, const ResourceManager&);

    nlohmann::json save(const Decal&, const ResourceManager&);
    void load(const nlohmann::json&, Decal&, const ResourceManager&);

    nlohmann::json save(const ParticleEmitter&);
    void load(const nlohmann::json&, ParticleEmitter&);

    nlohmann::json save(const IrradianceVolume&);
    void load(const nlohmann::json&, IrradianceVolume&);

    nlohmann::json save(const ReflectionProbe&);
    void load(const nlohmann::json&, ReflectionProbe&);

    nlohmann::json save(const UICanvas&);
    void load(const nlohmann::json&, UICanvas&);

    nlohmann::json save(const UIElement&);
    void load(const nlohmann::json&, UIElement&);

    nlohmann::json save(const UIImage&);
    void load(const nlohmann::json&, UIImage&);

    nlohmann::json save(const UIText&);
    void load(const nlohmann::json&, UIText&);

    nlohmann::json save(const UIButton&);
    void load(const nlohmann::json&, UIButton&);

    /**
     * @brief Hierarchy: only `parent` is serialized; sibling pointers are rebuilt
     * on load by re-running HierarchyOperations::setParent. The returned
     * JSON stores the parent's *old-file* entity index.
     */
    nlohmann::json save(const Hierarchy&);
    /**
     * @brief Read a saved Hierarchy's parent reference as an old-file entity index.
     *
     * The value is the parent's index in the file being loaded, to be remapped
     * to a live entity by the caller; a root entity yields uint32_t max.
     *
     * @param json The serialized Hierarchy object produced by save(const Hierarchy&).
     * @return The parent's old-file index, or uint32_t max for a root entity.
     */
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

} // namespace Vkm::Engine
