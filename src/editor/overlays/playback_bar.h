#pragma once

namespace Engine {

struct EditorContext;

/**
 * @brief Top-centre animation playback bar (Play/Pause + Stop).
 *
 * A small floating icon bar in the viewport (same look as the bottom-left
 * tool box) that drives global animation playback: Play/Pause toggles all
 * Animation components, Stop pauses and rewinds them.
 */
class ViewportPlaybar {
    public:
        void draw(EditorContext& ec);

        /// True while the mouse is over the bar (so the viewport does not
        /// also treat the click as a pick / camera input).
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
};

} // namespace Engine
