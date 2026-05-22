#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ui/editor_style.h"

namespace Engine {

/// Gizmo operation type. Select is a no-handle mode: picking only, no
/// manipulation handles drawn (GizmoOverlay skips manipulate() for it).
enum class GizmoOperation { Translate, Rotate, Scale, Select };

/// Gizmo coordinate space.
enum class GizmoMode { Local, World };

/// Which axis/element is hovered or active during interaction.
enum class GizmoElement : int {
    None = 0,
    AxisX, AxisY, AxisZ,
    PlaneYZ, PlaneXZ, PlaneXY,
};

/// Custom transform gizmo drawn via ImGui DrawList.
/// Non-copyable, non-movable. Owned by GizmoOverlay.
class TransformGizmo {
    public:
        TransformGizmo() = default;
        ~TransformGizmo() = default;

        TransformGizmo(const TransformGizmo& other) = delete;
        TransformGizmo& operator=(const TransformGizmo& other) = delete;

        TransformGizmo(TransformGizmo && other) = delete;
        TransformGizmo& operator=(TransformGizmo && other) = delete;

    public:
        /// Main entry point. Draws gizmo, handles interaction.
        /// Returns true if the model matrix was modified by a drag.
        bool manipulate(
            ImDrawList* drawList,
            const glm::mat4& view,
            const glm::mat4& projection,
            GizmoOperation operation,
            GizmoMode mode,
            glm::mat4& model,
            ImVec2 vpMin,
            float vpWidth,
            float vpHeight
        );

        bool isOver() const  { return m_hovered != GizmoElement::None; }
        bool isUsing() const { return m_dragging; }

        // Set snap angle in radians (0 = disabled). Applied during rotation drag.
        void setSnapAngle(float radians) { m_snapAngle = radians; }

        // Returns the delta quaternion from the current rotation drag (identity if not rotating).
        glm::quat getDragRotation() const { return m_dragRotation; }

    private:
        // Math utilities
        ImVec2 worldToScreen(const glm::vec3& worldPos) const;
        glm::vec3 screenToRay(ImVec2 screenPos) const;
        float computeScreenFactor(const glm::vec3& gizmoOrigin) const;
        static float intersectRayPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                       const glm::vec3& planePoint, const glm::vec3& planeNormal);
        static float distPointToSegment2D(ImVec2 p, ImVec2 a, ImVec2 b);

        // Hit testing
        GizmoElement hitTestTranslation(const glm::vec3 axes[3], const ImVec2 screenAxes[3]) const;
        GizmoElement hitTestRotation(const glm::vec3 axes[3]) const;
        GizmoElement hitTestScale(const ImVec2 screenAxes[3]) const;

        // Drawing
        void drawTranslationGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]);
        void drawRotationGizmo(ImDrawList* dl, const glm::vec3 axes[3]);
        void drawScaleGizmo(ImDrawList* dl, const ImVec2 screenAxes[3]);

        // Drag handling - return true if model was modified
        bool handleTranslationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleRotationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleScaleDrag(glm::mat4& model, const glm::vec3 axes[3]);

        // Helpers
        glm::vec3 getAxisDirection(GizmoElement elem, const glm::vec3 axes[3]) const;
        glm::vec3 getDragPlaneNormal(GizmoElement elem, const glm::vec3 axes[3]) const;
        ImU32 colorForElement(GizmoElement elem, GizmoElement highlight) const;

    private:
        // Per-frame cached state
        glm::mat4 m_view{1.0f};
        glm::mat4 m_projection{1.0f};
        glm::mat4 m_viewProj{1.0f};
        glm::mat4 m_invViewProj{1.0f};
        glm::vec3 m_cameraPos{0.0f};
        glm::vec3 m_cameraDir{0.0f};
        glm::vec3 m_gizmoOrigin{0.0f};
        float     m_screenFactor = 1.0f;
        float     m_uiScale      = 1.0f;  ///< DPI scale (ImGui font size / 13).
        ImVec2    m_originScreen{0, 0};
        ImVec2    m_mousePos{0, 0};
        ImVec2    m_vpMin{0, 0};
        float     m_vpWidth  = 0.0f;
        float     m_vpHeight = 0.0f;

        // Interaction state (persists across frames)
        GizmoElement m_hovered  = GizmoElement::None;
        GizmoElement m_active   = GizmoElement::None;
        bool         m_dragging = false;

        // Drag state
        glm::vec3 m_dragPlaneNormal{0.0f};
        glm::vec3 m_dragPlanePoint{0.0f};
        glm::vec3 m_dragStartWorldHit{0.0f};
        glm::mat4 m_dragStartModel{1.0f};

        // Decomposed start TRS, captured once at drag-start. Used by
        // handleScaleDrag so glm::decompose isn't re-run every frame
        // (which discarded skew + cost CPU for a constant input).
        glm::vec3 m_dragStartPos{0.0f};
        glm::quat m_dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_dragStartScale{1.0f};

        // Rotation-specific
        glm::vec3 m_rotationAxis{0.0f};
        glm::vec3 m_rotationStartDir{0.0f};

        // Scale-specific
        float m_scaleStartDist = 1.0f;

        // Snap
        float m_snapAngle = 0.0f;

        // Delta rotation from current drag (set by handleRotationDrag)
        glm::quat m_dragRotation{1.0f, 0.0f, 0.0f, 0.0f};

        // Style constants
        static constexpr float GIZMO_SIZE_PIXELS  = 110.0f;
        static constexpr float AXIS_HIT_RADIUS    = 10.0f;
        static constexpr float PLANE_QUAD_FRAC    = 0.28f;
        static constexpr float ARROW_HEAD_FRAC    = 0.15f;
        static constexpr float ARROW_HEAD_PIXELS  = 6.0f;
        static constexpr float SCALE_BOX_HALF     = 4.0f;
        static constexpr int   CIRCLE_SEGMENTS    = 64;
        static constexpr float LINE_THICKNESS      = 2.5f;
        static constexpr float HIGHLIGHT_THICKNESS = 3.5f;

        static constexpr ImU32 COLOR_X         = EditorStyle::AXIS_X_U32;
        static constexpr ImU32 COLOR_Y         = EditorStyle::AXIS_Y_U32;
        static constexpr ImU32 COLOR_Z         = EditorStyle::AXIS_Z_U32;
        static constexpr ImU32 COLOR_HIGHLIGHT = EditorStyle::HIGHLIGHT_U32;
        static constexpr ImU32 COLOR_PLANE_X   = EditorStyle::AXIS_X_FILL_U32;
        static constexpr ImU32 COLOR_PLANE_Y   = EditorStyle::AXIS_Y_FILL_U32;
        static constexpr ImU32 COLOR_PLANE_Z   = EditorStyle::AXIS_Z_FILL_U32;
};

} // namespace Engine
