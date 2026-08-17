#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

#include "framework/asset_picker.h"
#include "ui/editor_widgets.h"

namespace Engine {

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
 * card links out to the standalone Material Editor for full PBR editing.
 *
 * Entities belonging to a prefab instance are edited here like any other, and
 * an edit to one becomes a per-instance override (see PrefabOverrides): each
 * card marks the fields this instance owns and offers them back to the prefab.
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
        void drawCameraSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawReflectionProbeSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawDecalSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawParticleSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawIrradianceVolumeSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawWorldInspector(EditorContext& ec);
        void drawLODSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawAnimationSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawScriptSection(Scene& scene, EditorState& state, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawUICanvasSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIElementSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIImageSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUITextSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawUIButtonSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

        // Euler-angle edit cache for the Transform Rotation field, keyed by
        // entity. See EulerCache for the gimbal-lock rationale.
        EulerCache<EntityId> m_eulerCache;

        // World inspector's "Skybox HDR" browse. Cached file discovery rooted at
        // assets/envs; opened on demand instead of scanning every frame.
        AssetPicker m_envPicker;
};

} // namespace Engine
