#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

#include "framework/editor_panel.h"
#include "panels/environment_inspector.h"

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
class InspectorPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Inspector"; }
        void draw(EditorContext& ec) override;

    private:
        void drawTransformSection(Scene& scene, EntityId id);
        void drawMeshSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawLightSection(Scene& scene, EntityId id);
        void drawCameraSection(Scene& scene, EntityId id);
        void drawAnimationSection(Scene& scene, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawAddComponentMenu(Scene& scene, EntityId id);

        // The Environment singleton entity gets the whole rendering/post stack
        // here instead of the usual component cards.
        EnvironmentInspector m_environmentUI;

        // Euler-angle edit cache for the Transform Rotation field.
        // Quaternion->Euler is many-to-one and singular at +/-90 deg (gimbal
        // lock); re-deriving the display from t.rotation every frame makes X/Z
        // snap to +/-180 and Y jitter. The inspector instead keeps the edited
        // Euler as the source of truth and only re-seeds it when the rotation
        // changed externally (different entity, gizmo drag, scene load).
        EntityId  m_eulerFor{};
        glm::vec3 m_eulerDeg{0.0f};
};

} // namespace Engine
