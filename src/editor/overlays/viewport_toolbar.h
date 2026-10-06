#pragma once

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief The viewport's own controls: the tool strip and the view bar.
 *
 * Both sizes are fixed, so the overlays around them are placed without waiting a frame.
 */
class ViewportToolbar {
    public:
        ViewportToolbar() = default;
        ~ViewportToolbar() = default;

        ViewportToolbar(const ViewportToolbar& other) = delete;
        ViewportToolbar& operator=(const ViewportToolbar& other) = delete;

        ViewportToolbar(ViewportToolbar && other) = delete;
        ViewportToolbar& operator=(ViewportToolbar && other) = delete;

    public:
        /**
         * @brief Draw the tool strip (tools, space, snap) down the viewport's left edge.
         *
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

        /**
         * @brief Draw the view bar in the viewport's top-right corner.
         *
         * The RenderMode, the camera looked through, and Frame All and Focus.
         *
         * @param ec The frame's editor context.
         */
        void drawViewBar(EditorContext& ec);

        /**
         * @brief Whether the mouse was over the tool strip or the view bar as last drawn.
         *
         * @return True over either, so the viewport does not also take the click.
         */
        bool isHovered() const { return m_hovered || m_viewBarHovered; }

        /**
         * @brief The tool strip's outer width.
         *
         * @return Screen pixels.
         */
        static float toolStripWidth();

        /**
         * @brief The view bar's outer width.
         *
         * @return Screen pixels.
         */
        static float viewBarWidth();

        /**
         * @brief How far down the viewport the view bar reaches, inset included.
         *
         * @return Screen pixels from the viewport's top edge.
         */
        static float viewBarBottom();

    private:
        bool m_hovered        = false;  ///< Tool strip.
        bool m_viewBarHovered = false;  ///< View bar.
};

} // namespace Vkm::Engine
