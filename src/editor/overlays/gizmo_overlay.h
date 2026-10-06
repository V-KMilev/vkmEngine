#pragma once

#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>

#include <imgui.h>

#include "ecs/entity.h"
#include "ecs/component/core/transform.h"
#include "overlays/transform_gizmo.h"
#include "ui/editor_icons.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Viewport overlay: the transform gizmo, each kind's entity gizmos and markers, and click picking.
 */
class GizmoOverlay {
    public:
        GizmoOverlay() = default;
        ~GizmoOverlay() = default;

        GizmoOverlay(const GizmoOverlay& other) = delete;
        GizmoOverlay& operator=(const GizmoOverlay& other) = delete;

        GizmoOverlay(GizmoOverlay && other) = delete;
        GizmoOverlay& operator=(GizmoOverlay && other) = delete;

    public:
        /**
         * @brief Draw the translate/rotate/scale gizmo on the selected entity.
         *
         * One undo entry per drag. No-op for the Select tool, an empty selection or the
         * camera the frame renders through. Input ownership gates hover and drag, so a click
         * on a tool strip button does not start one.
         *
         * @param ec The frame's editor context.
         */
        void drawTransformGizmo(EditorContext& ec);

        /**
         * @brief Draw a small 3D shape per light entity (sun rays, point sphere, spot cone).
         *
         * Drawn behind the transform gizmo.
         *
         * @param ec The frame's editor context.
         */
        void drawLightGizmos(EditorContext& ec);

        /**
         * @brief Draw a frustum and icon per camera, except the one the frame renders through.
         *
         * @param ec The frame's editor context.
         */
        void drawCameraGizmos(EditorContext& ec);

        /**
         * @brief Draw each reflection probe's influence box and marker, and each irradiance volume's box.
         *
         * The selected volume also shows its probe grid, up to a cap.
         *
         * @param ec The frame's editor context.
         */
        void drawProbeGizmos(EditorContext& ec);

        /**
         * @brief Draw each decal's projection box and direction, and each emitter's marker and velocity.
         *
         * @param ec The frame's editor context.
         */
        void drawEffectGizmos(EditorContext& ec);

        /**
         * @brief Draw an icon per audio source and listener, the selected source's falloff spheres
         *        and the listener's facing.
         *
         * The speaker keeps its arcs only while spatial. Spheres for the selection only, or a
         * scene's worth buries everything else.
         *
         * @param ec The frame's editor context.
         */
        void drawAudioGizmos(EditorContext& ec);

        /**
         * @brief Draw a wireframe of every entity's physics Collider.
         *
         * Toggled by EditorState::showColliders.
         *
         * @param ec The frame's editor context.
         */
        void drawColliderGizmos(EditorContext& ec);

        /**
         * @brief Draw every joint as its two anchors and the line between them.
         *
         * Point joints mark the shared anchor; distance joints draw the rope.
         * Toggled with EditorState::showColliders.
         *
         * @param ec The frame's editor context.
         */
        void drawJointGizmos(EditorContext& ec);

        /**
         * @brief Draw every posed rig as bone segments from parent to child,
         * with an axis triad per bone on the selected one.
         *
         * Draws the pose SkeletalAnimationSystem published. Toggled by EditorState::showSkeletons.
         *
         * @param ec The frame's editor context.
         */
        void drawSkeletonGizmos(EditorContext& ec);

        /**
         * @brief Draw the world-space AABB of every visible entity.
         *
         * The visibility pass's set. Toggled by EditorState::showBounds.
         *
         * @param ec The frame's editor context.
         */
        void drawBoundsGizmos(EditorContext& ec);

        /**
         * @brief Outline every selected entity's world-space AABB as a selection cue.
         *
         * The active entity in full highlight, the rest dimmer; only what the visibility pass
         * drew. Lights, probes and cameras highlight their own gizmos.
         *
         * @param ec The frame's editor context.
         */
        void drawSelectionOutline(EditorContext& ec);

        /**
         * @brief Ray-cast pick on left-click in the viewport, updating the selection.
         *
         * Outside a session a blocking game UI element wins; then the nearest of the visible
         * meshes and this frame's markers. Empty space deselects. No-op unless input
         * ownership gives the picker the click, or while the gizmo has it. Never dirties the
         * scene.
         *
         * @param ec The frame's editor context.
         */
        void handleViewportPick(EditorContext& ec);
        bool isGizmoOver() const  { return m_gizmo.isOver(); }
        bool isGizmoUsing() const { return m_gizmo.isUsing(); }

    private:
        /**
         * @brief One entity marker drawn this frame, kept for the picker.
         */
        struct Marker {
            EntityId  id;
            ImVec2    screen;         ///< Where it was drawn.
            glm::vec3 world;          ///< Its distance ranks it against meshes.
            float     reach = 0.0f;   ///< Half-size of a world box that also answers; 0 none.
        };

    private:
        /**
         * @brief Close an active drag: push its undo entry and reset the overlay's and gizmo's state.
         *
         * @param ec Supplies the scene and the command stack.
         */
        void finishDrag(EditorContext& ec);

        /**
         * @brief Draw an entity's viewport marker and record it as a pick target.
         *
         * Every marker goes through here, so what is drawn is exactly what is pickable.
         *
         * @param dl Draw list to append to.
         * @param icon Glyph naming the entity's kind.
         * @param id Entity a click selects.
         * @param screen Projected position.
         * @param world The point it marks.
         * @param col Glyph colour.
         * @param reach Half-size of a world box around @p world that also answers, or 0.
         */
        void markEntity(
            ImDrawList* dl,
            EditorIcon icon,
            EntityId id,
            ImVec2 screen,
            const glm::vec3& world,
            ImU32 col,
            float reach = 0.0f
        );

    private:
        TransformGizmo m_gizmo;
        bool m_dragActive = false;

        // Snapshot at drag start, so finishDrag pushes one step for the whole drag.
        Transform m_dragStartTransform{};
        EntityId  m_dragEntity{};

        /**
         * @brief The active entity's parent-world basis as the drag began.
         *
         * Held, not recomputed: a selected ancestor moves with the drag, shifting the basis.
         * Identity when the active entity has no parent.
         */
        glm::mat4 m_dragStartParentWorld{1.0f};

        /**
         * @brief Drag-start transforms of the selection's roots, for one batch undo at drag end.
         *
         * Roots only: a descendant inherits the motion, so writing it would apply it twice.
         * The active entity is absent exactly when m_dragActiveIsDescendant. This, not the
         * selection size, says whether a drag is multi-entity.
         */
        std::vector<std::pair<EntityId, Transform>> m_dragSelection;

        /**
         * @brief Is the dragged entity itself a descendant of another selected one?
         *
         * Then the gizmo's direct write to it must be undone, or it travels twice.
         */
        bool m_dragActiveIsDescendant = false;

        /// This frame's markers, and the ImGui frame they were drawn in.
        std::vector<Marker> m_markers;
        int                 m_markerFrame = -1;
};

} // namespace Vkm::Engine
