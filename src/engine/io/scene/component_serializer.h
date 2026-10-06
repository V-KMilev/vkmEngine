#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity_mapping.h"
#include "ecs/environment.h"
#include "resource/asset_type.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

class Scene;

class ResourceManager;

/**
 * @brief Every component the scene format round-trips, one row each.
 *
 * P refers to nothing outside itself. R names assets, so save and load also take
 * the ResourceManager and an emitAssetRefs overload sits beside them. E names
 * other entities, so save takes an EntityNamer and load an EntityResolver.
 *
 * The key is the format, not the type name: ScriptComponent is stored as "Script".
 * Hierarchy is not a row: SceneSerializer reads it in pass 2, because the parent
 * it names may not exist when the entity is read.
 */
#define VKM_SCENE_COMPONENTS(P, R, E)             \
    P(Name,                "Name")                \
    P(Transform,           "Transform")           \
    P(Camera,              "Camera")              \
    P(Light,               "Light")               \
    P(Rigidbody,           "Rigidbody")           \
    R(Collider,            "Collider")            \
    P(CharacterController, "CharacterController") \
    E(Joint,               "Joint")               \
    E(Ragdoll,             "Ragdoll")             \
    R(Mesh,                "Mesh")                \
    R(LOD,                 "LOD")                 \
    R(Decal,               "Decal")               \
    P(ParticleEmitter,     "ParticleEmitter")     \
    R(AudioSource,         "AudioSource")         \
    P(AudioListener,       "AudioListener")       \
    P(IrradianceVolume,    "IrradianceVolume")    \
    P(ReflectionProbe,     "ReflectionProbe")     \
    P(Animation,           "Animation")           \
    R(Animator,            "Animator")            \
    P(BoneSocket,          "BoneSocket")          \
    P(ScriptComponent,     "Script")              \
    P(UICanvas,            "UICanvas")            \
    P(UIElement,           "UIElement")           \
    R(UIImage,             "UIImage")             \
    P(UIText,              "UIText")              \
    P(UIButton,            "UIButton")            \
    P(UIScroll,            "UIScroll")

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * A new component adds its save/load (and emitAssetRefs, if it references assets)
 * here and a row to VKM_SCENE_COMPONENTS. Asset handles are stored by name
 * (ResourceManager::findByName); Hierarchy::parent as the saved slot, which
 * resolves directly because SceneSerializer recreates each entity at its saved slot.
 */
namespace ComponentSerializer {

    nlohmann::json save(const PhysicsSettings&);
    void load(const nlohmann::json&, PhysicsSettings&);

    nlohmann::json save(const Name&);
    void load(const nlohmann::json&, Name&);

    /**
     * @brief The scene-global Environment (sky, night sky and fog).
     *
     * Missing keys keep the current values.
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
     * @brief Rigidbody: dynamics and material fields.
     *
     * The runtime sleep state (sleeping / sleepTimer) is not persisted.
     */
    nlohmann::json save(const Rigidbody&);
    void load(const nlohmann::json&, Rigidbody&);

    /**
     * @brief Collider: each part's shape tag plus the fields of every shape.
     *
     * A mesh part's triangles are built from its mesh on load (syncMeshCollider),
     * never written.
     */
    nlohmann::json save(const Collider&, const ResourceManager&);
    void load(const nlohmann::json&, Collider&, const ResourceManager&);

    /**
     * @brief CharacterController: the tuning only.
     *
     * The per-tick state is not saved: a half consumed jump request would replay
     * on load.
     */
    nlohmann::json save(const CharacterController&);
    void load(const nlohmann::json&, CharacterController&);

    /**
     * @brief Joint: the reflected fields, and `connected` as the carrier's name for it.
     */
    nlohmann::json save(const Joint&, const EntityNamer&);
    void load(const nlohmann::json&, Joint&, const EntityResolver&);

    /**
     * @brief Ragdoll: the switch, the group node, and the bone mapping.
     *
     * The bones are entities the scene saves anyway; this carries which body poses
     * which bone and the offset between them, naming entities as the carrier does.
     */
    nlohmann::json save(const Ragdoll&, const EntityNamer&);
    void load(const nlohmann::json&, Ragdoll&, const EntityResolver&);

    nlohmann::json save(const Mesh&, const ResourceManager&);
    void load(const nlohmann::json&, Mesh&, const ResourceManager&);

    /**
     * @brief Animator: the rig, the clip and how it is authored to play.
     *
     * The playback head and blend state are session state, not saved.
     */
    nlohmann::json save(const Animator&, const ResourceManager&);
    void load(const nlohmann::json&, Animator&, const ResourceManager&);

    /**
     * @brief BoneSocket: the bone an attachment rides and its offset on it.
     *
     * The bone name round-trips and the index is recovered at runtime: a stored
     * index silently addresses a neighbour once a re-export inserts a joint.
     */
    nlohmann::json save(const BoneSocket&);
    void load(const nlohmann::json&, BoneSocket&);

    nlohmann::json save(const LOD&, const ResourceManager&);
    void load(const nlohmann::json&, LOD&, const ResourceManager&);

    nlohmann::json save(const Decal&, const ResourceManager&);
    void load(const nlohmann::json&, Decal&, const ResourceManager&);

    nlohmann::json save(const ParticleEmitter&);
    void load(const nlohmann::json&, ParticleEmitter&);

    /**
     * @brief AudioSource: the clip it names and how it should be heard.
     *
     * `playing` and `started` are session state, not saved: a half finished sound
     * would resume on load. `playOnStart` is the authored half.
     */
    nlohmann::json save(const AudioSource&, const ResourceManager&);
    void load(const nlohmann::json&, AudioSource&, const ResourceManager&);

    nlohmann::json save(const AudioListener&);
    void load(const nlohmann::json&, AudioListener&);

    nlohmann::json save(const IrradianceVolume&);
    void load(const nlohmann::json&, IrradianceVolume&);

    nlohmann::json save(const ReflectionProbe&);
    void load(const nlohmann::json&, ReflectionProbe&);

    nlohmann::json save(const UICanvas&);
    void load(const nlohmann::json&, UICanvas&);

    nlohmann::json save(const UIElement&);
    void load(const nlohmann::json&, UIElement&);

    nlohmann::json save(const UIImage&, const ResourceManager&);
    void load(const nlohmann::json&, UIImage&, const ResourceManager&);

    nlohmann::json save(const UIText&);
    void load(const nlohmann::json&, UIText&);

    nlohmann::json save(const UIButton&);
    void load(const nlohmann::json&, UIButton&);

    nlohmann::json save(const UIScroll&);
    void load(const nlohmann::json&, UIScroll&);

    /**
     * @brief Hierarchy: only `parent` is serialized, as the parent's slot.
     *
     * Sibling links are rebuilt on load by HierarchyOperations::setParent.
     */
    nlohmann::json save(const Hierarchy&);
    /**
     * @brief Read a saved Hierarchy's parent reference as a saved slot.
     *
     * The caller links it once every entity exists at its saved slot. A root is
     * saved as slot 0, which no entity occupies; a block without the key yields
     * uint32_t max. Both read as a root.
     *
     * @param json Hierarchy object written by save(const Hierarchy&).
     * @return The parent's saved slot; 0 or uint32_t max for a root.
     */
    uint32_t loadParentIndex(const nlohmann::json& json);

    /**
     * @brief Animation: the three tracks, each with its easing by name, and playback state.
     */
    nlohmann::json save(const Animation&);
    void load(const nlohmann::json&, Animation&);

    /**
     * @brief ScriptComponent: each behavior by BehaviorRegistry type name, with
     *        its Behavior::visitFields `properties`.
     *
     * An unregistered type is kept as an UnknownBehavior so the next save writes
     * it back; a field whose key is absent keeps its default.
     */
    nlohmann::json save(const ScriptComponent&);
    void load(const nlohmann::json&, ScriptComponent&);

    /**
     * @brief Every asset a set of components references, one list per kind.
     *
     * The scene's `assets` block is built from it; a reference the block does not
     * name resolves to nothing on the next load. One list per kind because the
     * ResourceManager resolves a handle by its type. Empty handles are not recorded.
     */
    struct AssetRefs {
        std::vector<MeshHandle>          meshes;
        std::vector<MaterialHandle>      materials;
        std::vector<SkeletonHandle>      skeletons;
        std::vector<TextureHandle>       textures;
        std::vector<AnimationClipHandle> clips;
        std::vector<AudioClipHandle>     sounds;
    };

    /**
     * @brief Record every asset the component references into @p refs.
     *
     * One overload per R row of VKM_SCENE_COMPONENTS; a missing one is a compile
     * error at the expansion.
     */
    void emitAssetRefs(const Collider&,    AssetRefs&);
    void emitAssetRefs(const Mesh&,        AssetRefs&);
    void emitAssetRefs(const LOD&,         AssetRefs&);
    void emitAssetRefs(const Decal&,       AssetRefs&);
    void emitAssetRefs(const AudioSource&, AssetRefs&);
    void emitAssetRefs(const Animator&,    AssetRefs&);
    void emitAssetRefs(const UIImage&,     AssetRefs&);

    /**
     * @brief An asset name a load failed to resolve, and the field it was read from.
     *
     * The field is the scene format's key, so the caller can put the name back.
     */
    struct UnresolvedRef {
        std::string field;
        std::string name;
        AssetType   type = AssetType::Count;  ///< Which kind the loader looked it up as.
    };

    /**
     * @brief Take the references the loads since the last call could not resolve.
     *
     * An unresolved slot is left empty and the name is its only record; a save
     * without it writes an empty string over it. Collected here because a loader
     * has no room for a second output; drained by a caller that knows the entity
     * and component (see SceneSerializer::loadComponents). Call it inside an
     * UnresolvedScope. A reference read out of an array is not kept.
     *
     * @return The unresolved references, oldest first; empty when all resolved.
     */
    std::vector<UnresolvedRef> takeUnresolvedRefs();

    /**
     * @brief Bounds one component load's share of the unresolved list.
     *
     * Construct one around a single component load. The list is file-scope, so
     * the destructor drops whatever the load did not take: a loader that throws
     * part-way leaves no names for the next component read to claim.
     */
    class UnresolvedScope {
        public:
            UnresolvedScope() = default;
            ~UnresolvedScope();

            UnresolvedScope(const UnresolvedScope& other) = delete;
            UnresolvedScope& operator=(const UnresolvedScope& other) = delete;

            UnresolvedScope(UnresolvedScope && other) = delete;
            UnresolvedScope& operator=(UnresolvedScope && other) = delete;
    };

} // namespace ComponentSerializer

} // namespace Vkm::Engine
