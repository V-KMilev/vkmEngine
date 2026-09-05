#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/physics/authoring/ragdoll_build.h"

#include "framework/asset_picker.h"
#include "system/audio/audio_device.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;

/**
 * @brief Editor panel for inspecting and editing the selected entity's components.
 *
 * Displays collapsible sections for each component type (Transform, Mesh, Light,
 * Rigidbody, Collider, Camera, Reflection Probe, Animation, Script, Hierarchy)
 * with inline editing, plus an "Add Component" menu. When the World node is
 * selected instead of an entity, shows the scene-global Environment settings.
 * Edits route through the command stack so they are undoable. The compact Mesh
 * card hands full PBR editing to the Material tab beside it.
 *
 * Entities belonging to a prefab instance are edited here like any other, and
 * an edit to one becomes a per-instance override (see PrefabOverrides): each
 * card marks the fields this instance owns and offers them back to the prefab.
 *
 * Many cards name an authoring mistake where it is made, rather than leaving it
 * to the log. Which mistakes those are, what each one costs and why a given card
 * says it in DANGER rather than WARNING is one page: docs/reference/editor.md,
 * "A card names what its component is waiting for".
 */
/**
 * @brief Every component card, once: the component and the method that draws it.
 *
 * The header's declarations and `draw`'s dispatch are the same list of
 * twenty-six rows, in the same order, and both expand from here rather than
 * being kept in step by hand.
 *
 * Order is the order the cards appear in the panel, which is the order the
 * dispatch runs: Transform first because it is what an author looks at, the
 * hierarchy row last because it is about the entity rather than about what it
 * is made of.
 *
 * Every card takes the same pair - the context and the entity - so that this
 * list can exist at all. The context is what makes that possible: a card that
 * needs the audio device, the camera controller or the clock reaches them
 * through it rather than through a signature of its own.
 *
 * The last two columns are the card's heading and its accent colour. A
 * component's heading is wanted in five places - the card, its "Edit X" and
 * "Remove X" history entries, the Add Component menu item and that item's
 * "Add X" - and CardInfo below expands all five from this one, so a component
 * cannot be called one thing in the inspector and another in undo.
 */
#define VKM_INSPECTOR_CARDS(X)                                                        \
    X(Transform,           drawTransformSection,           "Transform",           Transform) \
    X(Mesh,                drawMeshSection,                "Mesh",                Mesh)      \
    X(Light,               drawLightSection,               "Light",               Light)     \
    X(Rigidbody,           drawRigidbodySection,           "Rigidbody",           Physics)   \
    X(Collider,            drawColliderSection,            "Collider",            Collider)  \
    X(CharacterController, drawCharacterControllerSection, "Character Controller", Physics)  \
    X(Joint,               drawJointSection,               "Joint",               Physics)   \
    X(Ragdoll,             drawRagdollSection,             "Ragdoll",             Physics)   \
    X(Camera,              drawCameraSection,              "Camera",              Camera)    \
    X(ReflectionProbe,     drawReflectionProbeSection,     "Reflection Probe",    Probe)     \
    X(Decal,               drawDecalSection,               "Decal",               Mesh)      \
    X(ParticleEmitter,     drawParticleSection,            "Particle Emitter",    Light)     \
    X(AudioSource,         drawAudioSourceSection,         "Audio Source",        Audio)     \
    X(AudioListener,       drawAudioListenerSection,       "Audio Listener",      Audio)     \
    X(IrradianceVolume,    drawIrradianceVolumeSection,    "Irradiance Volume",   Probe)     \
    X(LOD,                 drawLODSection,                 "LOD",                 Mesh)      \
    X(Animation,           drawAnimationSection,           "Animation",           Anim)      \
    X(Animator,            drawAnimatorSection,            "Animator",            Anim)      \
    X(BoneSocket,          drawBoneSocketSection,          "Bone Socket",         Anim)      \
    X(ScriptComponent,     drawScriptSection,              "Script",              Script)    \
    X(UICanvas,            drawUICanvasSection,            "UI Canvas",           UI)        \
    X(UIElement,           drawUIElementSection,           "UI Element",          UI)        \
    X(UIImage,             drawUIImageSection,             "UI Image",            UI)        \
    X(UIText,              drawUITextSection,              "UI Text",             UI)        \
    X(UIButton,            drawUIButtonSection,            "UI Button",           UI)        \
    X(Hierarchy,           drawHierarchySection,           "Hierarchy",           Hierarchy)

class InspectorPanel {
    public:
        InspectorPanel() = default;
        ~InspectorPanel() = default;

        InspectorPanel(const InspectorPanel& other) = delete;
        InspectorPanel& operator=(const InspectorPanel& other) = delete;

        InspectorPanel(InspectorPanel && other) = delete;
        InspectorPanel& operator=(InspectorPanel && other) = delete;

    public:
        void draw(EditorContext& ec);

    private:
        // The blank-panel and entity-identity rows, factored out so draw()
        // reads as the component-section dispatch that is its real job.
        void drawEmptySelectionState(EditorContext& ec);
        void drawIdentityHeader(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);

        // Each section takes the EditorState, so an edit cannot be made without
        // marking the scene dirty. The ResourceManager rides along wherever a card
        // can produce an override: serializing an asset ref resolves it to a name.
        void drawPrefabSection(EditorContext& ec, EntityId id);
        void drawWorldInspector(EditorContext& ec);

        // The twenty-six component cards, declared from the one list above so
        // that adding a card is a row rather than an edit in three files.
#define VKM_INSPECTOR_CARD_DECL(Component, method, title, accent) \
    void method(EditorContext& ec, EntityId id);
        VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_DECL)
#undef VKM_INSPECTOR_CARD_DECL

        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

    private:
        // Euler-angle edit cache for the Transform Rotation field, keyed by
        // entity. See EulerCache for the gimbal-lock rationale.
        EulerCache<EntityId> m_eulerCache;

        // The Bone Socket card's Offset rotation needs its own, keyed by the same
        // entity: one cache would have the two rotations reseeding each other.
        EulerCache<EntityId> m_socketEulerCache;

        // World inspector's "Skybox HDR" browse. Cached file discovery rooted at
        // assets/envs; opened on demand instead of scanning every frame.
        AssetPicker m_envPicker;

        // The clip currently being auditioned from a card, so a second press
        // replaces it rather than layering a second copy over the first.
        VoiceId m_previewVoice = 0;

        // Which card started it. The audition is stopped when the selection
        // leaves that entity, so the transport on the card always drives the
        // voice the card is showing.
        EntityId m_previewOwner;

        RagdollSettings m_ragdollSettings{};  ///< Proportions the Build button uses

        // The Connected combo's entries, rebuilt per frame because the list is
        // the scene. Members rather than locals so the per-frame churn reuses
        // one allocation instead of making two.
        /**
         * @brief The entity whose mesh-collider build found no whole triangle.
         *
         * An entity rather than a flag, so the warning belongs to that entity and
         * not to the panel, which outlives the selection.
         */
        EntityId                 m_meshColliderEmpty;
        std::vector<EntityId>    m_jointCandidates;
        std::vector<std::string> m_jointCandidateLabels;
        std::vector<const char*> m_jointCandidateNames;
        int m_colliderFitDetail = 4;  ///< Voxel resolution for the Collider "Fit to Mesh" button.
        int m_lodGenLevels      = 2;  ///< Levels the LOD card's Generate button builds below the source.
};

} // namespace Vkm::Engine
