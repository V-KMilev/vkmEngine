#pragma once

#include "ecs/entity.h"

#include "../editor_panel.h"

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
 * editor when a Mesh component is present. Stateless -- reads selectedEntity from EditorState.
 */
class InspectorPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Inspector"; }
        void draw(EditorContext& ec) override;

    private:
        void drawTransformSection(Scene& scene, EntityId id);
        void drawMeshSection(Scene& scene, ResourceManager& resources, EntityId id);
        void drawLightSection(Scene& scene, EntityId id);
        void drawCameraSection(Scene& scene, EntityId id);
        void drawAnimationSection(Scene& scene, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawAddComponentMenu(Scene& scene, EntityId id);
};

} // namespace Engine
