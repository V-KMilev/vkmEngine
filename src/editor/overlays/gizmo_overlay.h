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
        void drawTransformGizmo(EditorContext& ec);
        /// Draw a small 3D shape per light entity (sun rays, point sphere,
        /// spot cone) so the user can see where lights are without the
        /// Inspector. Drawn behind the transform gizmo.
        void drawLightGizmos(EditorContext& ec);

        /// Draw a frustum wireframe + billboard icon for every camera entity
        /// other than the one currently being flown (drawing one for the
        /// active camera would put the gizmo right on the viewer).
        void drawCameraGizmos(EditorContext& ec);
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
