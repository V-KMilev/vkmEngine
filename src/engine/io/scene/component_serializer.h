#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
#include "ecs/component/ui/ui_text.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Every component the scene format round-trips, one row each.
 *
 * P is a component whose save and load take only the component; R is one that
 * references assets by name, so both take the ResourceManager as well
 * (resolution happens against the staging RM on load) and an emitAssetRefs
 * overload sits beside them.
 *
 * The key is written out rather than derived from the type name, because it is
 * the format: ScriptComponent is stored as "Script", and a stringified type
 * name would change that silently.
 *
 * Saving, loading, the known-key set and the assets block that says what a
 * scene file needs all expand from this one list, so none of the four can
 * drift. A component saved but never loaded is silent round-trip data loss and
 * the unknown-key warning cannot catch it, because the key is known; an R row
 * whose assets nothing lists is the same loss one level down, because the
 * component's own key was written correctly and only the reference dies.
 *
 * Hierarchy is not a row: SceneSerializer::saveComponents writes it explicitly
 * and the caller's pass 2 reads it, because the parent it names may not exist
 * yet when the entity is read.
 */
#define VKM_SCENE_COMPONENTS(P, R)              \
    P(Name,             "Name")                 \
    P(Transform,        "Transform")            \
    P(Camera,           "Camera")               \
    P(Light,            "Light")                \
    P(Rigidbody,        "Rigidbody")            \
    P(Collider,         "Collider")             \
    P(CharacterController, "CharacterController") \
    R(Mesh,             "Mesh")                 \
    R(LOD,              "LOD")                  \
    R(Decal,            "Decal")                \
    P(ParticleEmitter,  "ParticleEmitter")      \
    R(AudioSource,      "AudioSource")          \
    P(AudioListener,    "AudioListener")        \
    P(IrradianceVolume, "IrradianceVolume")     \
    P(ReflectionProbe,  "ReflectionProbe")      \
    P(Animation,        "Animation")            \
    R(Animator,         "Animator")             \
    P(BoneSocket,       "BoneSocket")           \
    P(ScriptComponent,  "Script")               \
    P(UICanvas,         "UICanvas")             \
    P(UIElement,        "UIElement")            \
    P(UIImage,          "UIImage")              \
    P(UIText,           "UIText")               \
    P(UIButton,         "UIButton")

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * Each component type has a `save` and `load` overload, and one that references
 * assets has an `emitAssetRefs` overload beside them. Add a new component by
 * adding that set here and a row to VKM_SCENE_COMPONENTS above. Asset handles
 * (Mesh, Decal) are resolved by stable name through
 * ResourceManager::findByName; entity references (Hierarchy::parent) are stored
 * as the saved scene-table index, which resolves directly because
 * SceneSerializer recreates each entity at its saved slot.
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

    /**
     * @brief Collider: each part's shape tag plus the fields of every shape.
     */
    nlohmann::json save(const Collider&);
    void load(const nlohmann::json&, Collider&);

    /**
     * @brief CharacterController: the tuning only.
     *
     * moveInput, jumpRequested, grounded and groundNormal are per-tick traffic
     * between gameplay, the system and the solver - a scene row holding a half
     * consumed jump request would replay it on load.
     */
    nlohmann::json save(const CharacterController&);
    void load(const nlohmann::json&, CharacterController&);

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

    /**
     * @brief BoneSocket: the bone an attachment rides and its offset on it.
     *
     * The resolved bone index is deliberately absent. It is a property of the
     * rig currently loaded rather than of the authored socket - re-export the
     * character with a joint inserted and a stored index addresses its
     * neighbour, silently - so the name is what round-trips and the index is
     * recovered from it at runtime.
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
     * `playing` and `started` are deliberately absent. They describe a play
     * session rather than the authored scene, and a scene row holding a half
     * finished sound would resume a noise whose beginning nobody heard.
     * `playOnStart` is the authored half of the same thing.
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

    /**
     * @brief Every asset a set of components references, one list per kind.
     *
     * What the scene's `assets` block is built from: the block has to name each
     * of these for the next load to recreate them, and a reference it does not
     * name resolves to nothing.
     *
     * One list per kind rather than one type-erased list, because the reader
     * resolves each handle through the ResourceManager and that takes the
     * asset's type back. Empty handles are never recorded - a slot the author
     * left empty names nothing.
     */
    struct AssetRefs {
        std::vector<MeshHandle>          meshes;
        std::vector<MaterialHandle>      materials;
        std::vector<SkeletonHandle>      skeletons;
        std::vector<AnimationClipHandle> clips;
        std::vector<AudioClipHandle>     sounds;
    };

    /**
     * @brief Record every asset the component references into @p refs.
     *
     * One overload per component whose save writes a handle out as a name -
     * exactly the R rows of VKM_SCENE_COMPONENTS, which is what expands the
     * walk that calls these. That is why the set is nowhere written out by
     * hand: a component missing from such a walk still saves its handle as a
     * name, while the block listing what the scene needs never mentions it, so
     * the next load resolves the reference to nothing and neither end warns -
     * the component's own key was written correctly.
     *
     * A new R row with no overload here is a compile error at the expansion,
     * naming the component that needs one.
     */
    void emitAssetRefs(const Mesh&,        AssetRefs&);
    void emitAssetRefs(const LOD&,         AssetRefs&);
    void emitAssetRefs(const Decal&,       AssetRefs&);
    void emitAssetRefs(const AudioSource&, AssetRefs&);
    void emitAssetRefs(const Animator&,    AssetRefs&);

    /**
     * @brief An asset name a load has just failed to resolve, and the field it
     * was read from.
     *
     * The field is the scene format's key rather than the human word the error
     * message uses, because the caller's job with one of these is to put the
     * name back where it came from.
     */
    struct UnresolvedRef {
        std::string field;
        std::string name;
        AssetType   type = AssetType::Count;  ///< Which kind the loader looked it up as.
    };

    /**
     * @brief Take the references the loads since the last call could not resolve.
     *
     * A component's loader resolves names against the asset graph and leaves
     * the slot empty when one does not answer; the name is then the only record
     * of what belonged there, and a save that knows nothing about it writes an
     * empty string over it. Collected here rather than returned, because the
     * loaders are one overload per component with no room for a second output,
     * and drained by the scene loader, which knows which entity and which
     * component the names belong to.
     *
     * Call it inside an UnresolvedScope, which is what bounds the list to the
     * component it was filled for.
     *
     * Holds only the ones a save can put back: a reference read out of an array
     * rather than a named field has nowhere to return to, and is reported and
     * forgotten rather than kept.
     *
     * @return The unresolved references, oldest first; empty when all resolved.
     */
    std::vector<UnresolvedRef> takeUnresolvedRefs();

    /**
     * @brief Bounds one component load's share of the unresolved list.
     *
     * The list is a free list rather than a member, because the loaders are one
     * overload per component with nowhere to return a second value from - so it
     * outlives the component, the entity and the file it was filled for. The
     * destructor drops whatever the load did not take, and that is what makes a
     * loader that throws part-way safe: names left standing are drained by the
     * next component read instead, in the next scene opened, which records them
     * as its own and hands them to the next save's assets block.
     *
     * Construct one for the span of a single component load, before the loader
     * runs, and nothing else is needed - a load that finishes takes its own
     * references and leaves an empty list behind.
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
