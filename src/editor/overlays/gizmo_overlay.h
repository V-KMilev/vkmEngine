#pragma once

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "ecs/component/transform.h"
#include "gizmo/transform_gizmo.h"

namespace Engine {

struct EditorContext;

/**
 * @brief Viewport overlay for the transform gizmo and entity picking.
 *
 * Draws the translate/rotate/scale gizmo on the selected entity and handles
 * ray-cast entity picking on viewport click. Owns the TransformGizmo instance
 * and rotation drag state (avoiding matrix decomposition for quaternion stability).
 */
class GizmoOverlay {
    public:
        /**
         * @brief Draw the translate/rotate/scale gizmo on the selected entity.
         *
         * Applies drags back to its Transform (one undo entry per drag). No-op
         * for the Select tool, or when nothing (or the flown camera) is selected.
         */
        void drawTransformGizmo(EditorContext& ec);

        /**
         * @brief Draw a small 3D shape per light entity (sun rays, point
         * sphere, spot cone) so lights are findable without the Inspector.
         *
         * Drawn behind the transform gizmo.
         */
        void drawLightGizmos(EditorContext& ec);

        /**
         * @brief Draw a frustum wireframe + billboard icon for every camera
         * entity other than the one currently being flown.
         *
         * Drawing one for the active camera would put the gizmo on the viewer.
         */
        void drawCameraGizmos(EditorContext& ec);

        /**
         * @brief Draw the influence box + a centre marker for every reflection
         * probe, so probes are placeable and findable in the viewport.
         */
        void drawProbeGizmos(EditorContext& ec);

        /**
         * @brief Draw a wireframe of every entity's physics Collider (its set
         * of boxes) so the user sees what the solver collides against.
         *
         * Toggled by EditorState::showColliders.
         */
        void drawColliderGizmos(EditorContext& ec);

        /**
         * @brief Draw the world-space AABB of every visible entity.
         *
         * The set the visibility pass produced. Toggled by
         * EditorState::showBounds.
         */
        void drawBoundsGizmos(EditorContext& ec);

        /**
         * @brief Outline the selected entity's world-space AABB as a selection cue.
         *
         * Mesh entities only; lights / probes / cameras highlight their own gizmos.
         */
        void drawSelectionOutline(EditorContext& ec);

        void handleViewportPick(EditorContext& ec);
        bool isGizmoOver() const  { return m_gizmo.isOver(); }
        bool isGizmoUsing() const { return m_gizmo.isUsing(); }

    private:
        TransformGizmo m_gizmo;
        glm::quat m_dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
        bool m_dragActive = false;

        // Undo bookkeeping: snapshot the transform when a drag begins so
        // the drag-end can push one TransformChangeCommand covering the
        // whole drag rather than one per intermediate frame.
        Transform m_dragStartTransform{};
        EntityId  m_dragEntity{};
};

} // namespace Engine
