#pragma once

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include "transform_gizmo.h"

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
        void handleViewportPick(EditorContext& ec);
        bool isGizmoOver() const  { return m_gizmo.isOver(); }
        bool isGizmoUsing() const { return m_gizmo.isUsing(); }

    private:
        TransformGizmo m_gizmo;
        glm::quat m_dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
        bool m_dragActive = false;
        bool m_leftMouseWasDown = false;
};

} // namespace Engine
