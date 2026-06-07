#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "ecs/component/mesh_lod.h"  // MeshLOD::MAX_LEVELS for the decimate-grid cache

#include "framework/asset_picker.h"  // ReflectionProbe HDR Browse picker
#include "ui/editor_widgets.h"  // EulerCache

namespace Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;

/**
 * @brief Editor panel for inspecting and editing the selected entity's components.
 *
 * Displays collapsible sections for each component type (Transform, Mesh, Light,
 * Camera, Animation, Hierarchy) with inline editing. Includes a full PBR material
 * editor when a Mesh component is present. Stateless - reads selectedEntity from EditorState.
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
        // Each section takes the EditorState so it can flag the scene as dirty
        // when the user edits anything. Centralizing this avoids missing edits.
        void drawTransformSection(Scene& scene, EditorState& state, EntityId id);
        void drawMeshSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawMeshLODSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawLightSection(Scene& scene, EditorState& state, EntityId id);
        void drawRigidbodySection(Scene& scene, EditorState& state, EntityId id);
        void drawColliderSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawReflectionProbeSection(Scene& scene, EditorState& state, EntityId id);
        void drawCameraSection(Scene& scene, EditorState& state, EntityId id);
        void drawAnimationSection(Scene& scene, EditorState& state, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

        // Euler-angle edit cache for the Transform Rotation field, keyed by
        // entity. See EulerCache for the gimbal-lock rationale.
        EulerCache<EntityId> m_eulerCache;

        // Per-level grid resolution for the LOD section's "Decimate" button
        // (index 0 unused - level 0 is the base mesh, never decimated).
        int m_lodDecimateGrid[MeshLOD::MAX_LEVELS] = {16, 12, 8, 5};

        // File picker for the Reflection Probe HDR path Browse button. One
        // per panel so its on-open scan cache survives across frames.
        AssetPicker m_probeHdrPicker;
};

} // namespace Engine
