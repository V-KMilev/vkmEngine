#pragma once

namespace Engine {

struct FrameContext;

/**
 * @brief Top-centre transport bar (Play/Pause + Stop), engine-style.
 *
 * A small floating icon bar in the viewport (same look as the bottom-left
 * tool box) that drives global animation playback: Play/Pause toggles all
 * Animation components, Stop pauses and rewinds them. Replaces the old
 * "Pause/Resume All Animations" menu items and Statistics buttons.
 */
class ViewportPlaybar {
    public:
        void draw(FrameContext& ctx);

        /// True while the mouse is over the bar (so the viewport does not
        /// also treat the click as a pick / camera input).
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
};

} // namespace Engine
