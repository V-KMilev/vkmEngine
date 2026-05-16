#pragma once

namespace Engine {

struct EditorContext;

/**
 * @brief The editor's bottom status bar.
 *
 * Extracted from EditorSystem (god-file decomposition). Stateless readout of
 * the current selection (name + position) plus the build banner. Drawn by
 * EditorSystem as the last child inside the root window, after the panel
 * layout, so it sits at the bottom edge.
 */
class EditorStatusBar {
    public:
        void draw(EditorContext& ec);
};

} // namespace Engine
