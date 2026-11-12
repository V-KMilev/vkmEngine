#pragma once

#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>

#include <imgui.h>

#include "ecs/entity.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/scene/transform.h"
#include "overlays/gizmo/transform_gizmo.h"
#include "overlays/gizmo/ui_rect_drag.h"
#include "overlays/gizmo/entity_markers.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Viewport overlay: the transform gizmo, each kind's markers, the selection's UI rects and click
 *        picking.
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
         * @brief Draw the translate/rotate/scale gizmo on the selected entity, one undo entry per drag.
         *
         * No-op for the Select tool, an empty selection or the camera the frame renders through.
         *
         * @param ec The frame's editor context.
         */
        void drawTransformGizmo(EditorContext& ec);

        /**
         * @brief Hand the engine's DebugDraw a category per GizmoVisibility toggle, the focus and scale.
         *
         * @param ec    The frame's editor context.
         * @param shown False while the editor is hidden: no shapes, no focus.
         */
        void showWorldGizmos(EditorContext& ec, bool shown) const;

        /**
         * @brief Ray-cast pick on left-click in the viewport, updating the selection.
         *
         * Outside a session a blocking game UI element wins, then the nearest visible mesh or marker;
         * empty space deselects. No-op unless input ownership gives the picker the click. Never
         * dirties the scene.
         *
         * @param ec The frame's editor context.
         */
        void handleViewportPick(EditorContext& ec);

        /**
         * @brief Move and resize the active UI element by its rect, one undo step per drag.
         *
         * An edge or corner resizes it with the opposite side held (dragRect), in reference pixels.
         *
         * @param ec The frame's editor context.
         */
        void drawUIRectTool(EditorContext& ec);

        /// The viewport's entity markers, which the editor draws and the pick reads.
        EntityMarkers& markers() { return m_markers; }

        /// A rect's edge or corner counts; its body does not, so a click there still picks what is on top.
        bool isGizmoOver() const  { return m_gizmo.isOver() || rectEdgeHeld(m_rectHover); }
        bool isGizmoUsing() const { return m_gizmo.isUsing() || m_rectDrag != RectHandle::None; }

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
         * @brief Carry each dragged entity's scale into its Collider, from the drag's start.
         *
         * @param ec Supplies the scene.
         */
        void carryScale(EditorContext& ec);

    private:
        TransformGizmo m_gizmo;
        bool           m_dragActive  = false;
        bool           m_altCopied   = false;  ///< This drag's Alt copy was tried; one a drag.
        bool           m_altCopyMade = false;  ///< And it pushed a step, which an unmoved drag takes back.
        EntityId              m_altActive{};    ///< The active original an Alt copy was made of.
        std::vector<EntityId> m_altOriginals;   ///< The selection it was made of.
        bool                  m_altWasDirty{};  ///< Whether the scene had unsaved changes before it.

        Transform m_dragStartTransform{};  ///< At drag start, so finishDrag pushes one step per drag.
        EntityId  m_dragEntity{};

        glm::mat4 m_dragStartParentWorld{1.0f};  ///< The parent-world basis at drag start, held: a selected
                                                 ///< ancestor moves with the drag. Identity with no parent.

        std::vector<std::pair<EntityId, Transform>> m_dragSelection;  ///< The selection's roots at drag
                                                                      ///< start, for one batch undo; more
                                                                      ///< than one makes a multi-entity drag.

        bool m_dragActiveIsDescendant = false;  ///< The dragged entity descends from another selected one:
                                                ///< undo the gizmo's write to it, or it moves twice.

        std::vector<std::pair<EntityId, Collider>> m_dragColliders;  ///< A scale drag's entities' Colliders
                                                                     ///< at its start, which their parts
                                                                     ///< are carried from each frame.

        RectHandle m_rectHover = RectHandle::None;  ///< Under the pointer this frame.
        RectHandle m_rectDrag  = RectHandle::None;  ///< Held since the press.
        UIElement  m_rectStart{};                   ///< The element as the drag began.
        ImVec2     m_rectFrom{};                    ///< Where the press was, panel pixels.
        EntityId   m_rectEntity{};

        EntityMarkers m_markers;  ///< Drawn by the editor each frame; the pick reads them.
};

} // namespace Vkm::Engine
