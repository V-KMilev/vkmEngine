#pragma once

#include <utility>
#include <vector>

#include <imgui.h>

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
         * @brief Draw the authoring shapes of the effect components: each
         * decal's projection box (its Transform scale IS the box) with a line
         * along the projection direction, and a marker + velocity line per
         * particle emitter. Without these, an unselected decal or emitter is
         * invisible in the viewport.
         */
        void drawEffectGizmos(EditorContext& ec);

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

        /**
         * @brief Ray-cast pick on left-click in the viewport, updating the
         * editor selection.
         *
         * Tests the culled visible set (meshes) and enabled lights; nearest hit
         * wins, an empty-space click deselects. No-op while the gizmo is hovered
         * or being dragged. Selection is UI state only - it never dirties the scene.
         */
        void handleViewportPick(EditorContext& ec);
        bool isGizmoOver() const  { return m_gizmo.isOver(); }
        bool isGizmoUsing() const { return m_gizmo.isUsing(); }

    private:
        /**
         * @brief Close out an active gizmo drag: push its undo entry and reset
         * both the overlay's drag state and the gizmo's.
         *
         * @param ec Editor context supplying the scene and the command stack.
         */
        void finishDrag(EditorContext& ec);

    private:
        TransformGizmo m_gizmo;
        bool m_dragActive = false;

        // Undo bookkeeping: snapshot the transform when a drag begins so
        // the drag-end can push one TransformChangeCommand covering the
        // whole drag rather than one per intermediate frame.
        Transform m_dragStartTransform{};
        EntityId  m_dragEntity{};

        /**
         * @brief Drag-start transforms of EVERY selected entity (active
         * included), so a gizmo drag moves the whole selection and drag-end
         * can push one batch undo covering all of it.
         */
        std::vector<std::pair<EntityId, Transform>> m_dragSelection;

        /**
         * @brief Is the dragged entity itself a descendant of another selected one?
         *
         * The gizmo always writes the active entity's Transform - that is the
         * handle the user grabbed. When an ancestor is selected too, the motion
         * also arrives down the hierarchy, so that direct write has to be undone
         * or the entity travels twice.
         */
        bool m_dragActiveIsDescendant = false;
};

} // namespace Engine
