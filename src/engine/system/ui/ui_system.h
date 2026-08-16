#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/entity.h"
#include "ecs/component/ui_element.h"
#include "system/ui/ui_draw_data.h"

namespace Engine {

/**
 * @brief Lays out the UI hierarchy, builds the frame's 2D draw list, and routes
 *        pointer interaction.
 *
 * Runs in the Transform stage, right after HierarchySystem: UI layout is a
 * screen-space transform resolve, the 2D sibling of resolving WorldTransform.
 * Visible UICanvases are walked in ascending sortOrder, each parent-before-child,
 * resolving every UIElement's screenRect and appending its quads into a
 * UIDrawData the system owns. Buttons only record hit candidates during the
 * walk; once the whole draw list exists, resolveInteraction() picks the topmost
 * button under the pointer (last in painter order), drives the visual states,
 * and enqueues a UIClickEvent on release through the frame's EventBus
 * (ctx.events). The result is published on the FrameContext (ctx.ui) for the
 * RenderSystem to fold into the RenderView - the same hand-off the
 * VisibilitySystem uses for its culling result. Runs in both the editor and the
 * runtime.
 */
class UISystem : public System {
    public:
        UISystem() = default;
        ~UISystem() override = default;

        UISystem(const UISystem& other) = delete;
        UISystem& operator=(const UISystem& other) = delete;

        UISystem(UISystem && other) = delete;
        UISystem& operator=(UISystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        /**
         * @brief Notify the UI that the editor is capturing the pointer.
         *
         * The editor draws chrome (viewport toolbar, playbar, transform gizmo)
         * over the same rect the game UI lays out in, so a click aimed at that
         * chrome must not also press the button behind it. While capture is set
         * the walk still runs and the overlay still draws - only the hit-test is
         * suppressed. Called by EditorSystem each frame; the runtime leaves it
         * false.
         *
         * @param capture True while the editor owns the pointer this frame.
         */
        void setEditorPointerCapture(bool capture) { m_editorPointerCapture = capture; }

    private:
        /**
         * @brief A visible canvas queued for this frame's walk, sortable by draw order.
         */
        struct CanvasRef {
            int32_t  sortOrder;
            EntityId entity;
        };

        /**
         * @brief A button seen during the walk, resolved once the walk is done.
         *
         * The button's quad is emitted in walk (painter) order; its colour is
         * rewritten by resolveInteraction() once the topmost candidate is known.
         */
        struct ButtonHit {
            EntityId entity;       ///< The button entity.
            uint32_t firstVertex;  ///< Start of its 6-vertex quad in the draw list.
            bool     inside;       ///< Pointer inside the rect and the button interactable.
        };

        /**
         * @brief Resolve @p entity's screen rect, emit its visuals, then recurse.
         *
         * Children lay out relative to this element's resolved rect and are
         * emitted after it, so the draw list is in painter order (parent behind
         * child).
         *
         * @param ctx        The frame context (scene + component stores).
         * @param entity     The UIElement entity to resolve.
         * @param parentRect Parent rect in screen pixels.
         * @param scale      Canvas pixel scale (reference px -> screen px).
         */
        void resolveElement(
            FrameContext& ctx,
            EntityId entity,
            const UIRect& parentRect,
            float scale
        );

        /**
         * @brief Append the two-triangle quad for @p entity's UIImage, if present.
         */
        void emitImage(FrameContext& ctx, EntityId entity);

        /**
         * @brief Emit @p entity's UIButton quad and record it as a hit candidate.
         *
         * Interaction is deferred: the quad is emitted here to keep painter
         * order, but state and colour are settled by resolveInteraction() once
         * every candidate of the frame is known.
         */
        void emitButton(FrameContext& ctx, EntityId entity);

        /**
         * @brief Lay out @p entity's UIText into glyph quads, if present.
         *
         * One quad per visible glyph, then a single Text draw command carrying
         * the FontAsset's handle.
         *
         * @param ctx         The frame context (scene + resources).
         * @param entity      The UIText entity to lay out.
         * @param canvasScale Canvas pixel scale (reference px -> screen px).
         */
        void emitText(FrameContext& ctx, EntityId entity, float canvasScale);

        /**
         * @brief Settle this frame's pointer interaction across all buttons.
         *
         * Topmost wins: the last hit candidate under the pointer (painter
         * order) is the only one that hovers, presses, and - released over the
         * same button the press started on - fires a UIClickEvent. Every
         * button's visual state is written back and its quad recoloured to
         * match.
         */
        void resolveInteraction(FrameContext& ctx);

        /**
         * @brief Append a two-triangle quad spanning @p p0..p1 with @p uv0..uv1.
         */
        void appendQuad(
            const glm::vec2& p0,
            const glm::vec2& p1,
            const glm::vec2& uv0,
            const glm::vec2& uv1,
            const glm::vec4& color
        );

    private:
        UIDrawData m_drawData;            ///< Reused frame draw list; published through ctx.ui.

        std::vector<CanvasRef> m_canvases;    ///< This frame's visible canvases, sorted; capacity reused.
        std::vector<ButtonHit> m_buttonHits;  ///< This frame's button candidates in painter order; capacity reused.

        // Pointer interaction state.
        EntityId  m_pressedButton{};      ///< Button a press started over (the click candidate).
        bool      m_prevMouseDown = false;  ///< Last frame's button state, for edge detection.
        glm::vec2 m_pointer{0.0f};        ///< Pointer in viewport-local pixels this frame.
        bool      m_mouseDown     = false;  ///< Primary button held this frame.
        bool      m_mouseDownEdge = false;  ///< Pressed this frame (was up).
        bool      m_mouseUpEdge   = false;  ///< Released this frame (was down).

        bool      m_editorPointerCapture = false;  ///< Editor owns the pointer; skip hit-testing.
};

} // namespace Engine
