#pragma once

#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>

#include <imgui.h>

#include "ecs/entity.h"
#include "ecs/component/core/transform.h"
#include "overlays/transform_gizmo.h"
#include "overlays/ui_rect_drag.h"
#include "ui/editor_icons.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Viewport overlay: the transform gizmo, each kind's markers, the selection's UI rects and
 *        click picking; the world's gizmo shapes are the engine's (showWorldGizmos).
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
         * @brief Hand the engine's DebugDraw what the viewport shows: a category per
         *        GizmoVisibility toggle, the selection as the focus, the editor's scale.
         *
         * The world's shapes are drawn into the scene by the engine (DebugDraw); this overlay keeps
         * the markers that pick and the 2D tools.
         *
         * @param ec    The frame's editor context.
         * @param shown False while the editor is hidden, which leaves the scene the whole window:
         *              none of its shapes, no focus.
         */
        void showWorldGizmos(EditorContext& ec, bool shown) const;

        /**
         * @brief Mark each light with its icon in its color; a box sized by its reach answers a click too.
         *
         * @param ec The frame's editor context.
         */
        void markLights(EditorContext& ec);

        /**
         * @brief Mark each camera with its icon, except the one the frame renders through.
         *
         * @param ec The frame's editor context.
         */
        void markCameras(EditorContext& ec);

        /**
         * @brief Mark each reflection probe, planar reflector and irradiance volume.
         *
         * @param ec The frame's editor context.
         */
        void markProbes(EditorContext& ec);

        /**
         * @brief Mark each decal and particle emitter.
         *
         * @param ec The frame's editor context.
         */
        void markEffects(EditorContext& ec);

        /**
         * @brief Mark each audio source and listener: a source faded while it has no clip and
         *        ringed while it plays, the listeners other than the ear faded.
         *
         * @param ec The frame's editor context.
         */
        void markAudio(EditorContext& ec);

        /**
         * @brief Mark each joint halfway along its rope, or at its anchor while it is off.
         *
         * Shown with GizmoVisibility::colliders.
         *
         * @param ec The frame's editor context.
         */
        void markJoints(EditorContext& ec);

        /**
         * @brief Outline each selected UI element's resolved rect, the active one in full highlight.
         *
         * A UI element is no object for the engine's selection boxes.
         *
         * @param ec The frame's editor context.
         */
        void drawUIRects(EditorContext& ec);

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

        /**
         * @brief Move and resize the active UI element by its rect in the viewport.
         *
         * Its body drags it, an edge or corner handle resizes it with the opposite side
         * held (dragRect), in its canvas's reference pixels. One undo step per drag.
         *
         * @param ec The frame's editor context.
         */
        void drawUIRectTool(EditorContext& ec);

        /// A rect's edge or corner counts; its body does not, so a click there still picks what is on top.
        bool isGizmoOver() const  { return m_gizmo.isOver() || rectEdgeHeld(m_rectHover); }
        bool isGizmoUsing() const { return m_gizmo.isUsing() || m_rectDrag != RectHandle::None; }

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
        /// Whether @p handle resizes: an edge or a corner, not the body or nothing.
        static bool rectEdgeHeld(RectHandle handle) {
            return handle != RectHandle::None && handle != RectHandle::Body;
        }

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
         * @param col Glyph color.
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
        bool m_dragActive  = false;
        bool m_altCopied   = false;  ///< This drag's Alt copy was tried; one a drag.
        bool m_altCopyMade = false;  ///< And it pushed a step, which an unmoved drag takes back.
        EntityId              m_altActive{};    ///< The active original an Alt copy was made of.
        std::vector<EntityId> m_altOriginals;   ///< The selection it was made of.

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

        RectHandle m_rectHover = RectHandle::None;  ///< Under the pointer this frame.
        RectHandle m_rectDrag  = RectHandle::None;  ///< Held since the press.
        UIElement  m_rectStart{};                   ///< The element as the drag began.
        ImVec2     m_rectFrom{};                    ///< Where the press was, panel pixels.
        EntityId   m_rectEntity{};

        /// This frame's markers, and the ImGui frame they were drawn in.
        std::vector<Marker> m_markers;
        int                 m_markerFrame = -1;
};

} // namespace Vkm::Engine
