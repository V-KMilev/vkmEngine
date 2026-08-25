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

        // Each section takes the EditorState so it can flag the scene as dirty
        // when the user edits anything. Centralizing this avoids missing edits.
        // The ResourceManager rides along wherever a card can produce a prefab
        // override: the override's value is the field's serialized JSON, and
        // serializing an asset reference resolves its handle to a name.
        void drawPrefabSection(Scene& scene, EditorState& state, EntityId id);
        void drawTransformSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawMeshSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawLightSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawRigidbodySection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawColliderSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        // The Camera card takes the whole context for the same reason the audio
        // cards do: which camera is rendered from is the camera controller's
        // answer, and it is not reachable from the scene alone.
        void drawCameraSection(EditorContext& ec, EntityId id);
        void drawReflectionProbeSection(Scene& scene, ResourceManager& resources,
                                        EditorState& state, EntityId id);
        void drawDecalSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        // Particles and Animation take it for the clock: both cards report on a
        // simulation that only advances while the world runs, and the clock is
        // what separates "nothing to show" from "not running".
        void drawParticleSection(EditorContext& ec, EntityId id);
        // The audio cards take the whole context: the clip preview button plays
        // through the editor's audio device, which is not reachable from the
        // scene or the asset graph.
        void drawAudioSourceSection(EditorContext& ec, EntityId id);
        void drawAudioListenerSection(EditorContext& ec, EntityId id);
        void drawIrradianceVolumeSection(Scene& scene, ResourceManager& resources,
                                         EditorState& state, EntityId id);
        void drawWorldInspector(EditorContext& ec);
        void drawLODSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawAnimationSection(EditorContext& ec, EntityId id);
        void drawAnimatorSection(EditorContext& ec, EntityId id);
        void drawBoneSocketSection(Scene& scene, ResourceManager& resources,
                                   EditorState& state, EntityId id);
        void drawCharacterControllerSection(Scene& scene, ResourceManager& resources,
                                            EditorState& state, EntityId id);

        /**
         * @brief Joint: what it holds, to what, and where on each body.
         *
         * The connected entity is picked from the scene rather than typed,
         * because a joint to a slot that does not exist is a joint that does
         * nothing and says nothing.
         */
        void drawJointSection(Scene& scene, ResourceManager& resources,
                              EditorState& state, EntityId id);

        /**
         * @brief Ragdoll: the switch, and the buttons that build and clear it.
         *
         * Building needs the rig, which comes from the entity's Animator - so
         * the card says so when there is none, rather than offering a button
         * that cannot work.
         */
        void drawRagdollSection(Scene& scene, ResourceManager& resources,
                                EditorState& state, EntityId id);
        void drawScriptSection(Scene& scene, EditorState& state, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawUICanvasSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIElementSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIImageSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUITextSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIButtonSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
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
        bool                     m_meshColliderEmpty = false;
        std::vector<EntityId>    m_jointCandidates;
        std::vector<std::string> m_jointCandidateLabels;
        std::vector<const char*> m_jointCandidateNames;
        int m_colliderFitDetail = 4;  ///< Voxel resolution for the Collider "Fit to Mesh" button.
        int m_lodGenLevels      = 2;  ///< Levels the LOD card's Generate button builds below the source.
};

} // namespace Vkm::Engine
