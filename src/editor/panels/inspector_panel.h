#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

#include "panels/environment_inspector.h"
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
        void draw(EditorContext& ec);

    private:
        // Each section takes the EditorState so it can flag the scene as dirty
        // when the user edits anything. Centralizing this avoids missing edits.
        void drawTransformSection(Scene& scene, EditorState& state, EntityId id);
        void drawMeshSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawLightSection(Scene& scene, EditorState& state, EntityId id);
        void drawCameraSection(Scene& scene, EditorState& state, EntityId id);
        void drawAnimationSection(Scene& scene, EditorState& state, EntityId id);
        void drawHierarchySection(Scene& scene, EditorState& state, EntityId id);
        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

        // The Environment singleton entity gets the whole rendering/post stack
        // here instead of the usual component cards.
        EnvironmentInspector m_environmentUI;

        // Euler-angle edit cache for the Transform Rotation field, keyed by
        // entity. See EulerCache for the gimbal-lock rationale.
        EulerCache<EntityId> m_eulerCache;
};

} // namespace Engine
