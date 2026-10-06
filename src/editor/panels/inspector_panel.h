#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "system/physics/authoring/ragdoll_build.h"

#include "ui/asset_picker.h"
#include "system/audio/audio_device.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;
class SceneIOController;

/**
 * @brief Every component card, once: the component and the method that draws it.
 *
 * Cards are declared and dispatched from this list, in panel order. Every card takes
 * the context and the entity, so anything more comes through the context.
 *
 * The last two columns are heading and accent. CardInfo builds the title and the
 * Add, Edit and Remove history labels from the heading, so inspector and undo agree.
 */
#define VKM_INSPECTOR_CARDS(X)                                                        \
    X(Transform,           drawTransformSection,           "Transform",           TRANSFORM) \
    X(Mesh,                drawMeshSection,                "Mesh",                MESH)      \
    X(Light,               drawLightSection,               "Light",               LIGHT)     \
    X(Rigidbody,           drawRigidbodySection,           "Rigidbody",           PHYSICS)   \
    X(Collider,            drawColliderSection,            "Collider",            COLLIDER)  \
    X(CharacterController, drawCharacterControllerSection, "Character Controller", PHYSICS)  \
    X(Joint,               drawJointSection,               "Joint",               PHYSICS)   \
    X(Ragdoll,             drawRagdollSection,             "Ragdoll",             PHYSICS)   \
    X(Camera,              drawCameraSection,              "Camera",              CAMERA)    \
    X(ReflectionProbe,     drawReflectionProbeSection,     "Reflection Probe",    PROBE)     \
    X(Decal,               drawDecalSection,               "Decal",               MESH)      \
    X(ParticleEmitter,     drawParticleSection,            "Particle Emitter",    LIGHT)     \
    X(AudioSource,         drawAudioSourceSection,         "Audio Source",        AUDIO)     \
    X(AudioListener,       drawAudioListenerSection,       "Audio Listener",      AUDIO)     \
    X(IrradianceVolume,    drawIrradianceVolumeSection,    "Irradiance Volume",   PROBE)     \
    X(LOD,                 drawLODSection,                 "LOD",                 MESH)      \
    X(Animation,           drawAnimationSection,           "Animation",           ANIM)      \
    X(Animator,            drawAnimatorSection,            "Animator",            ANIM)      \
    X(BoneSocket,          drawBoneSocketSection,          "Bone Socket",         ANIM)      \
    X(ScriptComponent,     drawScriptSection,              "Script",              SCRIPT)    \
    X(UICanvas,            drawUICanvasSection,            "UI Canvas",           UI)        \
    X(UIElement,           drawUIElementSection,           "UI Element",          UI)        \
    X(UIImage,             drawUIImageSection,             "UI Image",            UI)        \
    X(UIText,              drawUITextSection,              "UI Text",             UI)        \
    X(UIButton,            drawUIButtonSection,            "UI Button",           UI)        \
    X(UIScroll,            drawUIScrollSection,            "UI Scroll",           UI)        \
    X(Hierarchy,           drawHierarchySection,           "Hierarchy",           HIERARCHY) \

/**
 * @brief Editor panel for inspecting and editing the selected entity's components.
 *
 * The World node shows the Environment and physics settings instead. Edits are undoable.
 * An edit to a prefab instance becomes an override (see PrefabOverrides), marked on its
 * card. Cards name authoring mistakes in place: docs/reference/editor.md, "A card names
 * what its component is waiting for".
 */
class InspectorPanel {
    public:
        /// Euler edit cache per quaternion a behavior card has shown, keyed by address.
        using BehaviorEulers = std::unordered_map<const glm::quat*, EulerCache<int>>;

    public:
        InspectorPanel() = default;
        ~InspectorPanel() = default;

        InspectorPanel(const InspectorPanel& other) = delete;
        InspectorPanel& operator=(const InspectorPanel& other) = delete;

        InspectorPanel(InspectorPanel && other) = delete;
        InspectorPanel& operator=(InspectorPanel && other) = delete;

    public:
        /**
         * @brief Draw the selection's cards, or the World's, or the empty state.
         *
         * @param ec The frame's editor context.
         * @param sceneIO Play state: decides whether a previewed pose edits the authored scene.
         */
        void draw(EditorContext& ec, const SceneIOController& sceneIO);

    private:
        void drawEmptySelectionState(EditorContext& ec);
        void drawIdentityHeader(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);

        void drawPrefabSection(EditorContext& ec, EntityId id);
        void drawWorldInspector(EditorContext& ec);

#define VKM_INSPECTOR_CARD_DECL(Component, method, title, accent) \
    void method(EditorContext& ec, EntityId id);
        VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_DECL)
#undef VKM_INSPECTOR_CARD_DECL

        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

    private:
        /// Transform Rotation's Euler cache; see EulerCache.
        EulerCache<EntityId> m_eulerCache;

        /// Bone Socket Offset's own, or the two rotations would reseed each other.
        EulerCache<EntityId> m_socketEulerCache;

        BehaviorEulers m_behaviorEulers;

        /// World inspector's "Skybox HDR" browse.
        AssetPicker m_envPicker;

        /// Remembered so a second press replaces the audition rather than layering.
        VoiceId m_previewVoice = 0;

        /// The audition's entity; it stops when the selection leaves it.
        EntityId m_previewOwner;

        RagdollSettings m_ragdollSettings{};  ///< For the Build button

        /**
         * @brief The entity whose mesh-collider build found no whole triangle.
         *
         * An entity, not a flag: the panel outlives the selection.
         */
        EntityId m_meshColliderEmpty;

        /// Whether a play session holds the scene, for this frame's cards.
        bool m_sessionPlaying = false;

        int m_colliderFitDetail = 4;  ///< "Fit to Mesh" voxel resolution.
        int m_lodGenLevels      = 2;  ///< LOD levels Generate builds below the source.
};

} // namespace Vkm::Engine
