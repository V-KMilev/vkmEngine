#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "system/ocean/ocean_spectrum.h"
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
 * @brief The headings Add Component lists its components under, in its order: ecs/component/'s
 *        folders, plus Script from system/script/.
 */
enum class AddSection { Core, Render, Animation, Audio, Physics, UI, Script, Count };

/**
 * @brief Every component card, once: the component and the method that draws it.
 *
 * Cards are declared and dispatched from this list, in panel order. Every card takes
 * the context and the entity, so anything more comes through the context.
 *
 * The last four columns are heading, accent, the Add Component heading it goes under
 * (AddSection) and what Add Component says the component is for; nullptr there leaves it out
 * of Add Component. CardInfo builds the title and the Add, Edit and Remove history labels
 * from the heading, so inspector and undo agree.
 */
#define VKM_INSPECTOR_CARDS(X)                                                                                                                                            \
    X(Transform,           drawTransformSection,           "Transform",            TRANSFORM, Core,      "Where it is, how it is turned and its scale")                   \
    X(Mesh,                drawMeshSection,                "Mesh",                 MESH,      Render,    "Draws a mesh in a material")                                    \
    X(Light,               drawLightSection,               "Light",                LIGHT,     Render,    "A sun, a lamp, a spot or an area light")                        \
    X(Rigidbody,           drawRigidbodySection,           "Rigidbody",            PHYSICS,   Physics,   "Moved by physics: falls, is pushed, collides")                  \
    X(Collider,            drawColliderSection,            "Collider",             COLLIDER,  Physics,   "The shape physics sees")                                        \
    X(CharacterController, drawCharacterControllerSection, "Character Controller", PHYSICS,   Physics,   "Walks, climbs steps and jumps under a player's input")          \
    X(Buoyancy,            drawBuoyancySection,            "Buoyancy",             PHYSICS,   Physics,   "Floats on water and is carried by a river")                     \
    X(Joint,               drawJointSection,               "Joint",                PHYSICS,   Physics,   "Ties this body to another: a hinge, a rope")                    \
    X(Ragdoll,             drawRagdollSection,             "Ragdoll",              PHYSICS,   Physics,   "Bodies on a rig's bones, to fall limp")                         \
    X(Camera,              drawCameraSection,              "Camera",               CAMERA,    Render,    "A view the game can draw through")                              \
    X(ReflectionProbe,     drawReflectionProbeSection,     "Reflection Probe",     PROBE,     Render,    "What shiny things nearby reflect")                              \
    X(PlanarReflector,     drawPlanarReflectorSection,     "Planar Reflector",     PROBE,     Render,    "A true mirror or still water: reflects the scene in its plane") \
    X(Decal,               drawDecalSection,               "Decal",                MESH,      Render,    "A picture projected onto what lies in its box")                 \
    X(ParticleEmitter,     drawParticleSection,            "Particle Emitter",     LIGHT,     Render,    "Sparks, smoke, fire: particles it spawns")                      \
    X(ParticleTrigger,     drawParticleTriggerSection,     "Particle Trigger",     LIGHT,     Render,    "Bursts an emitter when something enters it")                    \
    X(AudioSource,         drawAudioSourceSection,         "Audio Source",         AUDIO,     Audio,     "Plays a sound from here")                                       \
    X(AudioListener,       drawAudioListenerSection,       "Audio Listener",       AUDIO,     Audio,     "Where sound is heard from: usually the camera")                 \
    X(IrradianceVolume,    drawIrradianceVolumeSection,    "Irradiance Volume",    PROBE,     Render,    "Baked bounce light for the space in its box")                   \
    X(Ocean,               drawOceanSection,               "Ocean",                MESH,      Render,    "The scene's sea: the waves a Sea material shows")               \
    X(Terrain,             drawTerrainSection,             "Terrain",              TERRAIN,   Render,    nullptr)                                                         \
    X(LOD,                 drawLODSection,                 "LOD",                  MESH,      Render,    "Simpler meshes for when it is far off")                         \
    X(Animation,           drawAnimationSection,           "Animation",            ANIM,      Animation, "Plays a clip on its transform: a door, a bob")                  \
    X(Animator,            drawAnimatorSection,            "Animator",             ANIM,      Animation, "Poses a rigged mesh with clips")                                \
    X(BoneSocket,          drawBoneSocketSection,          "Bone Socket",          ANIM,      Animation, "Follows a bone of a rig: a sword in a hand")                    \
    X(ScriptComponent,     drawScriptSection,              "Script",               SCRIPT,    Script,    "The project's behaviors: gameplay in C++")                      \
    X(UICanvas,            drawUICanvasSection,            "UI Canvas",            UI,        UI,        "A layer of UI drawn over the scene")                            \
    X(UIElement,           drawUIElementSection,           "UI Element",           UI,        UI,        "A rectangle placed on a canvas")                                \
    X(UIImage,             drawUIImageSection,             "UI Image",             UI,        UI,        "Fills its rectangle with a color or a picture")                 \
    X(UIText,              drawUITextSection,              "UI Text",              UI,        UI,        "Text in its rectangle")                                         \
    X(UIButton,            drawUIButtonSection,            "UI Button",            UI,        UI,        "Clicked, it sends an event")                                    \
    X(UIScroll,            drawUIScrollSection,            "UI Scroll",            UI,        UI,        "Scrolls what lies under it")                                    \
    X(UISlider,            drawUISliderSection,            "UI Slider",            UI,        UI,        "A value dragged along a track")                                 \
    X(UIToggle,            drawUIToggleSection,            "UI Toggle",            UI,        UI,        "On or off")                                                     \
    X(UIChoice,            drawUIChoiceSection,            "UI Choice",            UI,        UI,        "One of a list, stepped with arrows")                            \
    X(NetLocal,            drawNetLocalSection,            "Net Local",            HIERARCHY, Core,      "Kept off the wire: every end makes its own")                    \
    X(Hierarchy,           drawHierarchySection,           "Hierarchy",            HIERARCHY, Core,      nullptr)                                                         \

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

#define VKM_INSPECTOR_CARD_DECL(Component, method, title, accent, section, hint) \
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

        /// The Ocean card's sea height, made again only when its shape changes: a swell is costly.
        std::optional<OceanShape> m_seaShape;  ///< Empty until the card first draws.
        float                     m_seaHeight = 0.0f;

        /// The Script card's capture before this frame's edits, taken again only once an edit, the
        /// entity or the world moved it (m_scriptKey), or every frame of a session, whose behaviors
        /// change themselves.
        std::string                            m_scriptBefore;
        std::optional<std::array<uint64_t, 3>> m_scriptKey;  ///< Empty until the first capture.
};

} // namespace Vkm::Engine
