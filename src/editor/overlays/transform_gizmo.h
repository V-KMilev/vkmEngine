#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/bounds.h"
#include "overlays/gizmo_snap.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief What the gizmo manipulates, one per set of handles it can draw.
 *
 * There is no Select here: that is a *tool* the author picks (see EditorTool).
 */
enum class GizmoOperation { Translate, Rotate, Scale };

/**
 * @brief Gizmo coordinate space.
 */
enum class GizmoMode { Local, World };

/**
 * @brief Which axis/element is hovered or active during interaction.
 */
enum class GizmoElement : int {
    None = 0,
    AxisX, AxisY, AxisZ,
    PlaneYZ, PlaneXZ, PlaneXY,
};

/**
 * @brief Axis / plane elements by index (0 = X, 1 = Y, 2 = Z), shared by the
 * hit-test and draw loops so they agree on the index -> element mapping.
 */
inline constexpr GizmoElement GIZMO_AXES[3] = {GizmoElement::AxisX, GizmoElement::AxisY, GizmoElement::AxisZ};
inline constexpr GizmoElement GIZMO_PLANES[3] = {
    GizmoElement::PlaneYZ,
    GizmoElement::PlaneXZ,
    GizmoElement::PlaneXY
};

/**
 * @brief Custom transform gizmo drawn via ImGui DrawList.
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
         * @param pointerFree Whether the pointer is on the scene rather than on
         *                    something drawn over it. False, no handle is hovered
         *                    and no drag begins; a drag already under way carries on.
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
         * @brief End the current drag from outside, as a mouse release would.
         *
         * manipulate() is the only place a drag normally ends, so a caller that
         * stops drawing the gizmo mid-drag (tool switched, selection changed)
         * has to say so - otherwise m_dragging latches and the gizmo reports
         * itself in use forever.
         */
        void endDrag() {
            m_dragging = false;
            m_active = GizmoElement::None;
            m_dragRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        /**
         * @brief Drop every trace of interaction, for a frame that does not run one.
         *
         * manipulate() is the only thing that maintains the hover, so a frame
         * that never reaches it leaves whatever the last one decided standing.
         * isOver() then goes on reporting a handle under the cursor for as long
         * as the gizmo stays undrawn.
         *
         * The release path inside manipulate() wants endDrag() rather than this:
         * the pointer really is still over the handle it just let go of, and the
         * next frame's hit test is what should say otherwise.
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
         * @brief Get the delta rotation accumulated by the current rotation drag.
         *
         * @return Delta quaternion from the active rotation drag, or identity
         *         when no rotation is in progress.
         */
        glm::quat getDragRotation() const { return m_dragRotation; }

    private:
        // Design pixels at EditorStyle::REFERENCE_FONT_SIZE, read through
        // EditorStyle::px, which rounds the scaled size to whole pixels - so a
        // fraction here shows only once the font scales it past a boundary.
        static constexpr float GIZMO_SIZE_PIXELS   = 127.0f;
        static constexpr float AXIS_HIT_RADIUS     = 11.5f;
        /// A fraction of the axis length, not a pixel size.
        static constexpr float PLANE_QUAD_FRAC     = 0.28f;
        static constexpr float ARROW_HEAD_PIXELS   = 7.0f;
        static constexpr float SCALE_BOX_HALF      = 4.6f;
        static constexpr float CENTRE_DOT_RADIUS   = 3.5f;
        static constexpr int   CIRCLE_SEGMENTS     = 64;
        static constexpr float LINE_THICKNESS      = 2.9f;
        static constexpr float HIGHLIGHT_THICKNESS = 4.0f;

        static constexpr ImU32 COLOR_X           = EditorStyle::AXIS_X_U32;
        static constexpr ImU32 COLOR_Y           = EditorStyle::AXIS_Y_U32;
        static constexpr ImU32 COLOR_Z           = EditorStyle::AXIS_Z_U32;
        static constexpr ImU32 COLOR_HIGHLIGHTED = EditorStyle::HIGHLIGHT_U32;
        static constexpr ImU32 COLOR_PLANE_X     = EditorStyle::AXIS_X_FILL_U32;
        static constexpr ImU32 COLOR_PLANE_Y     = EditorStyle::AXIS_Y_FILL_U32;
        static constexpr ImU32 COLOR_PLANE_Z     = EditorStyle::AXIS_Z_FILL_U32;

    private:
        /**
         * @brief Project a world point into viewport screen coordinates.
         *
         * False means the point is behind the near plane, where it has no
         * screen position at all: @p out is left untouched, and every handle
         * derived from the point has to be dropped rather than drawn or hit
         * tested at a stand-in coordinate.
         *
         * @param worldPos Point in world space.
         * @param out      Screen position inside the viewport rect, written
         *                 only on success.
         * @return True when the point is in front of the near plane.
         */
        bool project(const glm::vec3& worldPos, ImVec2& out) const;

        /**
         * @brief The world ray under a screen point, cast from the near plane.
         *
         * From the near plane and not from the camera's position, which an
         * orthographic ray never passes through.
         *
         * @param screenPos Point in screen coordinates.
         * @return The ray through the viewport at that point.
         */
        Math::Ray screenToRay(ImVec2 screenPos) const;

        /**
         * @brief Unit direction from a world point back toward the viewer.
         *
         * Back along the ray that lands on the point, so it is the eye's
         * direction under a perspective camera and the view axis under an
         * orthographic one. What a ring segment's facing is measured against,
         * by the draw and the hit test alike.
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

        // Return true if model was modified.
        bool handleTranslationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleRotationDrag(glm::mat4& model, const glm::vec3 axes[3]);
        bool handleScaleDrag(glm::mat4& model, const glm::vec3 axes[3]);

        /**
         * @brief Index into an axes[3] for an axis element, or -1 for anything else.
         *
         * @param elem The element to resolve.
         * @return 0, 1 or 2 for AxisX/Y/Z; -1 for a plane or None.
         */
        static int axisIndex(GizmoElement elem);

        glm::vec3 getAxisDirection(GizmoElement elem, const glm::vec3 axes[3]) const;
        glm::vec3 getDragPlaneNormal(GizmoElement elem, const glm::vec3 axes[3]) const;
        ImU32 colorForElement(GizmoElement elem, GizmoElement highlight) const;

        /**
         * @brief Screen-space corners of translation plane quad @p i.
         *
         * The quad spans the two axes other than i: @p qA / @p qB sit on
         * those axes at PLANE_QUAD_FRAC, @p qC is the far corner. One source
         * of truth for the hit test and the draw, including whether there is
         * a quad at all - there is none when a spanning axis is behind the
         * near plane and has no screen position to span to.
         *
         * @param i          Index of the axis the quad faces along.
         * @param screenAxes Projected axis tips, indexed as GIZMO_AXES.
         * @param axisOk     Which of those tips projected.
         * @param qA         Corner on axis (i + 1) % 3.
         * @param qB         Corner on axis (i + 2) % 3.
         * @param qC         Far corner, offset by both.
         * @return True when both spanning axes projected and the corners hold.
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
        // Per-frame cached state
        glm::mat4 m_viewProj{1.0f};
        glm::mat4 m_invViewProj{1.0f};
        glm::vec3 m_cameraDir{0.0f};
        glm::vec3 m_cameraRight{1.0f, 0.0f, 0.0f};
        glm::vec3 m_gizmoOrigin{0.0f};
        float     m_screenFactor = 1.0f;
        ImVec2    m_originScreen{0, 0};
        ImVec2    m_mousePos{0, 0};
        ImVec2    m_vpMin{0, 0};
        float     m_vpWidth  = 0.0f;
        float     m_vpHeight = 0.0f;

        // Interaction state (persists across frames)
        GizmoElement m_hovered  = GizmoElement::None;
        GizmoElement m_active   = GizmoElement::None;
        bool         m_dragging = false;

        glm::vec3 m_dragPlaneNormal{0.0f};
        glm::vec3 m_dragPlanePoint{0.0f};
        glm::vec3 m_dragStartWorldHit{0.0f};
        glm::mat4 m_dragStartModel{1.0f};

        // Decomposed start TRS, captured once at drag-start.
        glm::vec3 m_dragStartPos{0.0f};
        glm::quat m_dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_dragStartScale{1.0f};

        glm::vec3 m_rotationAxis{0.0f};
        glm::vec3 m_rotationStartDir{0.0f};

        float m_scaleStartDist = 1.0f;

        GizmoSnap m_snap;

        /// Delta rotation from current drag (set by handleRotationDrag)
        glm::quat m_dragRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace Vkm::Engine
