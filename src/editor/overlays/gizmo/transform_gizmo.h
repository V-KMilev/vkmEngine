#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/reflect/enum_names.h"
#include "base/math/ray.h"
#include "overlays/gizmo/gizmo_snap.h"

namespace Vkm::Engine {

/**
 * @brief The handles the gizmo draws; selecting is an EditorTool, not an operation.
 */
enum class GizmoOperation { Translate, Rotate, Scale };

/**
 * @brief Whether the handles follow the model's axes or the world's.
 */
enum class GizmoMode {
    Local,
    World,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief A handle of the gizmo, an axis or a plane, as hovered or dragged.
 */
enum class GizmoElement : int {
    None = 0,
    AxisX, AxisY, AxisZ,
    PlaneYZ, PlaneXZ, PlaneXY,
};

// Elements by axis index (0 = X, 1 = Y, 2 = Z), shared by the hit tests and the draws.
inline constexpr GizmoElement GIZMO_AXES[3] = {GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ};
    ///< The arrows, by the axis each moves along.
inline constexpr GizmoElement GIZMO_PLANES[3] = {
    GizmoElement::PlaneYZ,
    GizmoElement::PlaneXZ,
    GizmoElement::PlaneXY
};  ///< The plane handles, by the axis each plane's normal is.

/**
 * @brief Transform gizmo drawn into an ImGui draw list.
 */
class TransformGizmo {
    public:
        TransformGizmo() = default;
        ~TransformGizmo() = default;

        TransformGizmo(const TransformGizmo& other) = delete;
        TransformGizmo& operator=(const TransformGizmo& other) = delete;

        TransformGizmo(TransformGizmo && other) = delete;
        TransformGizmo& operator=(TransformGizmo && other) = delete;

    public:
        /**
         * @brief Draw the gizmo and run one frame of interaction on it.
         *
         * @param drawList    Draw list the handles are appended to.
         * @param view        The view matrix the viewport renders with.
         * @param projection  The projection it renders with.
         * @param operation   Which handles to draw.
         * @param mode        Whether the handles follow the model's axes or the world's.
         * @param model       World matrix of what is manipulated; written by a drag.
         * @param vpMin       Top-left of the viewport rect, in screen pixels.
         * @param vpWidth     Viewport width, in screen pixels.
         * @param vpHeight    Viewport height, in screen pixels.
         * @param pointerFree False when something drawn over the scene is under the pointer: no
         *                    handle hovers and no drag begins; a drag under way carries on.
         * @return True when a drag modified @p model this frame.
         */
        bool manipulate(
            ImDrawList* drawList,
            const glm::mat4& view,
            const glm::mat4& projection,
            GizmoOperation operation,
            GizmoMode mode,
            glm::mat4& model,
            ImVec2 vpMin,
            float vpWidth,
            float vpHeight,
            bool pointerFree
        );

        bool isOver() const  { return m_hovered != GizmoElement::None; }
        bool isUsing() const { return m_dragging; }

        /**
         * @brief End the current drag, as a mouse release would.
         *
         * A caller that stops drawing the gizmo mid-drag must call it, or isUsing() stays true.
         */
        void endDrag() {
            m_dragging     = false;
            m_active       = GizmoElement::None;
            m_dragRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        /**
         * @brief Drop the hover and any drag, for a frame that does not call manipulate().
         *
         * Without it isOver() keeps reporting the last hover while the gizmo is undrawn. A release
         * inside manipulate() wants endDrag(): the pointer is still over the handle.
         */
        void cancelInteraction() {
            m_hovered = GizmoElement::None;
            endDrag();
        }

        /**
         * @brief Set the steps the next drags move in.
         *
         * @param snap One step per operation; a default GizmoSnap snaps nothing.
         */
        void setSnap(const GizmoSnap& snap) { m_snap = snap; }

        /**
         * @brief The rotation the current rotation drag has applied.
         *
         * @return Identity when no rotation drag is under way.
         */
        glm::quat getDragRotation() const { return m_dragRotation; }

    private:
        // Design pixels at EditorStyle::REFERENCE_FONT_SIZE; EditorStyle::px scales and rounds
        // them to whole pixels, so a fraction shows only at a larger font.
        static constexpr float GIZMO_SIZE_PIXELS   = 127.0f;
        static constexpr float AXIS_HIT_RADIUS     = 11.5f;
        static constexpr float PLANE_QUAD_FRAC     = 0.28f;  ///< A fraction of the axis length, not pixels.
        static constexpr float ARROW_HEAD_PIXELS   = 7.0f;
        static constexpr float SCALE_BOX_HALF      = 4.6f;
        static constexpr float CENTER_DOT_RADIUS   = 3.5f;
        static constexpr int   CIRCLE_SEGMENTS     = 64;
        static constexpr float LINE_THICKNESS      = 2.9f;
        static constexpr float HIGHLIGHT_THICKNESS = 4.0f;

    private:
        /**
         * @brief Project a world point into viewport screen coordinates.
         *
         * @param worldPos Point in world space.
         * @param out      Screen position, written only on success.
         * @return False behind the near plane, where the point has no screen position: drop every
         *         handle derived from it.
         */
        bool project(const glm::vec3& worldPos, ImVec2& out) const;

        /**
         * @brief The world ray under a screen point.
         *
         * @param screenPos Point in screen pixels.
         * @return A ray cast from the near plane: an orthographic ray never passes through the camera.
         */
        Math::Ray screenToRay(ImVec2 screenPos) const;

        /**
         * @brief Unit direction from a world point back along the ray that lands on it.
         *
         * The eye's direction in perspective, the view axis in ortho. The draw and the hit test both
         * measure a ring segment's facing against it.
         *
         * @param worldPos Point in world space, in front of the near plane.
         * @return The direction toward the viewer.
         */
        glm::vec3 toViewer(const glm::vec3& worldPos) const;

        float computeScreenFactor(const glm::vec3& gizmoOrigin) const;
        static float intersectRayPlane(
            const Math::Ray& ray,
            const glm::vec3& planePoint,
            const glm::vec3& planeNormal
        );
        static float distPointToSegment2D(ImVec2 p, ImVec2 a, ImVec2 b);

        GizmoElement hitTestTranslation(const ImVec2 screenAxes[3], const bool axisOk[3]) const;
        GizmoElement hitTestRotation(const glm::vec3 axes[3]) const;
        GizmoElement hitTestScale(const ImVec2 screenAxes[3], const bool axisOk[3]) const;

        void drawTranslationGizmo(ImDrawList* dl, const ImVec2 screenAxes[3], const bool axisOk[3]);
        void drawRotationGizmo(ImDrawList* dl, const glm::vec3 axes[3]);
        void drawScaleGizmo(ImDrawList* dl, const ImVec2 screenAxes[3], const bool axisOk[3]);

        // True when they modified model.
        bool handleTranslationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleRotationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleScaleDrag(glm::mat4& model, const glm::vec3 axes[3]);

        /**
         * @brief Index into an axes[3] for an axis element.
         *
         * @param elem Any element, a plane or None included.
         * @return 0, 1 or 2 for AxisX/Y/Z; -1 for a plane or None.
         */
        static int axisIndex(GizmoElement elem);

        glm::vec3 getAxisDirection(GizmoElement elem, const glm::vec3 axes[3]) const;
        glm::vec3 getDragPlaneNormal(GizmoElement elem, const glm::vec3 axes[3]) const;
        ImU32 colorForElement(GizmoElement elem, GizmoElement highlight) const;

        /**
         * @brief Screen-space corners of translation plane quad @p i, for the hit test and the draw.
         *
         * @param i          Index of the axis the quad faces along.
         * @param screenAxes Projected axis tips, indexed as GIZMO_AXES.
         * @param axisOk     Which of those tips projected.
         * @param qA         Corner on axis (i + 1) % 3, at PLANE_QUAD_FRAC.
         * @param qB         Corner on axis (i + 2) % 3, at PLANE_QUAD_FRAC.
         * @param qC         Far corner, offset by both.
         * @return False when a spanning axis is behind the near plane: there is no quad.
         */
        bool planeQuadCorners(
            int i,
            const ImVec2 screenAxes[3],
            const bool axisOk[3],
            ImVec2& qA,
            ImVec2& qB,
            ImVec2& qC
        ) const;

    private:
        glm::mat4 m_viewProj{1.0f};
        glm::mat4 m_invViewProj{1.0f};
        glm::vec3 m_cameraDir{0.0f};
        glm::vec3 m_cameraRight{1.0f, 0.0f, 0.0f};
        glm::vec3 m_gizmoOrigin{0.0f};
        float     m_screenFactor = 1.0f;
        ImVec2    m_originScreen{0, 0};
        ImVec2    m_mousePos{0, 0};
        ImVec2    m_vpMin{0, 0};
        float     m_vpWidth      = 0.0f;
        float     m_vpHeight     = 0.0f;

        GizmoElement m_hovered  = GizmoElement::None;
        GizmoElement m_active   = GizmoElement::None;
        bool         m_dragging = false;

        glm::vec3 m_dragPlaneNormal{0.0f};
        glm::vec3 m_dragPlanePoint{0.0f};
        glm::vec3 m_dragStartWorldHit{0.0f};
        glm::mat4 m_dragStartModel{1.0f};

        glm::vec3 m_dragStartPos{0.0f};
        glm::quat m_dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_dragStartScale{1.0f};

        glm::vec3 m_rotationAxis{0.0f};
        glm::vec3 m_rotationStartDir{0.0f};

        float m_scaleStartDist = 1.0f;

        GizmoSnap m_snap;

        glm::quat m_dragRotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< Set by handleRotationDrag.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::GizmoMode, "Local", "World")
