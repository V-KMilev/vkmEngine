#pragma once

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/entity.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_shape.h"
#include "system/ui/ui_draw_data.h"

namespace Vkm::Engine {

/**
 * @brief Lays out the UI hierarchy, builds the frame's 2D draw list, and routes pointer interaction.
 *
 * Runs in the Transform stage, after BehaviorSystem writes UI components and before
 * RenderSystem draws the ctx.ui it publishes. Reads no WorldTransform. Canvases walk
 * in ascending sortOrder; a clip rect descends with the walk and bounds both drawing
 * and the pointer.
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
         */
        struct ButtonHit {
            EntityId entity;
            uint32_t firstVertex;  ///< Start of its 6-vertex quad.
        };

        /**
         * @brief A scroll view the pointer is inside, as the walk entered it.
         */
        struct ScrollTarget {
            EntityId entity;
            size_t   blockersBefore;  ///< Blockers under the pointer on entry.
        };

    private:
        /**
         * @brief Resolve @p entity's screen rect, emit its visuals, then recurse.
         *
         * The return is what a UIScroll measures content by: what was drawn, not empty
         * rects. A clipping subtree reports its own rect, since what it hides extends nothing.
         *
         * @param ctx        Frame context supplying the scene and the asset graph.
         * @param entity     Element to resolve; one with no UIElement draws nothing.
         * @param parentRect Parent rect in screen pixels, already scroll-offset.
         * @param clip       What this element may draw inside, in screen pixels.
         * @param scale      Canvas pixel scale (reference px -> screen px).
         * @param depth      Depth below the canvas; bounded by HierarchyOperations::MAX_DEPTH.
         * @return The far corner of everything this subtree drew, in screen pixels, or
         *         the lowest representable corner when it drew nothing.
         */
        glm::vec2 resolveElement(
            FrameContext& ctx,
            EntityId entity,
            const UIRect& parentRect,
            const UIRect& clip,
            float scale,
            uint32_t depth
        );

        /**
         * @brief Add a run of vertices to the draw list, merging it where it can.
         *
         * Adjacent runs under one clip merge unless they sample different atlases or
         * images; a flat solid samples neither and a glyph no image.
         *
         * @param first First vertex of the run.
         * @param count Vertices in it.
         * @param font  Atlas the run samples; empty for solids.
         * @param image Image the run samples; empty for text and flat solids.
         * @param clip  Scissor rect; runs under different clips never merge.
         */
        void appendCommand(
            uint32_t first,
            uint32_t count,
            FontHandle font,
            TextureHandle image,
            const UIRect& clip
        );

        /**
         * @brief Append the two-triangle quad for @p entity's UIImage, if present.
         *
         * @param ctx         Frame context supplying the scene.
         * @param entity      Entity that may carry the UIImage.
         * @param element     Its UIElement, already resolved this frame.
         * @param clip        What it may draw inside, in screen pixels.
         * @param canvasScale Its canvas's reference-to-screen pixel scale.
         * @return Whether it had one, and so drew a quad.
         */
        bool emitImage(
            FrameContext& ctx,
            EntityId entity,
            const UIElement& element,
            const UIRect& clip,
            float canvasScale
        );

        /**
         * @brief Emit @p entity's UIButton quad and record it as a hit candidate.
         *
         * @param ctx         Frame context supplying the scene.
         * @param entity      Entity that may carry the UIButton.
         * @param element     Its UIElement, already resolved this frame.
         * @param clip        What it may draw inside, in screen pixels.
         * @param canvasScale Its canvas's reference-to-screen pixel scale.
         * @return Whether it had one, and so drew a quad.
         */
        bool emitButton(
            FrameContext& ctx,
            EntityId entity,
            const UIElement& element,
            const UIRect& clip,
            float canvasScale
        );

        /**
         * @brief Lay out @p entity's UIText into glyph quads, if present, as one command run.
         *
         * @param ctx         Frame context supplying the scene and the font.
         * @param entity      Entity that may carry the UIText.
         * @param element     Its UIElement, already resolved this frame.
         * @param clip        What it may draw inside, in screen pixels.
         * @param canvasScale Its canvas's reference-to-screen pixel scale.
         * @return The far corner of the block it drew, in screen pixels, or
         *         the lowest representable corner when it drew nothing.
         */
        glm::vec2 emitText(
            FrameContext& ctx,
            EntityId entity,
            const UIElement& element,
            const UIRect& clip,
            float canvasScale
        );

        /**
         * @brief Break @p text into the lines it draws as, into m_lines.
         *
         * When wrapping, breaks at the last space that fits, mid-word if one word does
         * not fit. A trailing newline yields a final empty line.
         *
         * @param text     The UIText's string; the lines view into it.
         * @param font     Font whose kerned advances measure it (walkGlyphs).
         * @param scale    Baked pixels to screen pixels for that font.
         * @param maxWidth Width to wrap at, in screen pixels.
         * @param wrap     Whether a line breaks at @p maxWidth as well as at a newline.
         */
        void breakLines(std::string_view text, const FontAsset& font, float scale, float maxWidth, bool wrap);

        /**
         * @brief Settle this frame's pointer interaction across all buttons, and route the wheel.
         *
         * Only the topmost blocker can hover or press; a release over the button the
         * press began on fires a UIClickEvent.
         *
         * @param ctx Frame context supplying the scene, the input map and the bus.
         */
        void resolveInteraction(FrameContext& ctx);

        /**
         * @brief Append a two-triangle quad spanning @p p0..p1 with @p uv0..uv1.
         *
         * @param p0     Top-left corner, in screen pixels.
         * @param p1     Bottom-right corner, in screen pixels.
         * @param uv0    Coordinates at @p p0.
         * @param uv1    Coordinates at @p p1.
         * @param color  The fill at the top edge.
         * @param bottom The fill at the bottom edge; equal to @p color for a flat quad.
         * @param shape  Corner radius and border width, in screen pixels; UI_TEXT_MARK's
         *               radius for a glyph.
         * @param border The border's colour; unused where @p shape gives no border.
         * @param image  Whether the quad is tinted by its run's image (UIVertex::image).
         */
        void appendQuad(
            const glm::vec2& p0,
            const glm::vec2& p1,
            const glm::vec2& uv0,
            const glm::vec2& uv1,
            const glm::vec4& color,
            const glm::vec4& bottom,
            const glm::vec2& shape,
            const glm::vec4& border,
            bool image
        );

        /**
         * @brief Colour the quad appendQuad wrote at @p first, top edge to bottom.
         *
         * The one place that knows which of the six vertices lie on which edge.
         *
         * @param first  First of the quad's six vertices.
         * @param top    The fill at the top edge.
         * @param bottom The fill at the bottom edge; equal to @p top for a flat quad.
         */
        void recolourQuad(uint32_t first, const glm::vec4& top, const glm::vec4& bottom);

        /**
         * @brief Append a solid quad over @p rect drawn the way @p shape says.
         *
         * @param rect  The quad, in screen pixels.
         * @param color The fill at its top edge.
         * @param shape Corners, border and fade, in reference pixels.
         * @param scale Reference-to-screen pixel scale.
         * @param image Whether the quad is tinted by its run's image.
         */
        void appendShaped(
            const UIRect& rect,
            const glm::vec4& color,
            const UIShape& shape,
            float scale,
            bool image
        );

    private:
        UIDrawData m_drawData;            ///< Published through ctx.ui.

        std::vector<CanvasRef> m_canvases;    ///< Visible canvases, sorted.
        /// Button candidates, painter order.
        std::vector<ButtonHit> m_buttonHits;

        /**
         * @brief Scroll views whose window the pointer is inside, in painter order.
         *
         * Last is innermost, so it takes the wheel.
         */
        std::vector<ScrollTarget> m_scrollTargets;

        /// One UIText's lines, viewing into its string.
        std::vector<std::string_view> m_lines;

        /// Everything under the pointer that blocks it, buttons included, in painter order.
        std::vector<EntityId> m_pointerBlockers;

        EntityId  m_pressedButton{};        ///< Button a press started over.
        glm::vec2 m_pointer{0.0f};          ///< Pointer in viewport-local pixels this frame.
        float     m_wheel         = 0.0f;   ///< Wheel notches this frame, positive away from the viewer.
        bool      m_mouseDown     = false;  ///< Primary button held this frame.
        bool      m_mouseDownEdge = false;  ///< Pressed this frame (was up).
        bool      m_mouseUpEdge   = false;  ///< Released this frame (was down).
        bool      m_cursorFree    = false;  ///< Not a look control, so its position is a place on screen.
        bool      m_pointerIsOurs = false;  ///< Free, and the host's chrome does not own it this frame.
};

} // namespace Vkm::Engine
