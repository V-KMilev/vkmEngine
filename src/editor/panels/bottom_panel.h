#pragma once

#include <cstddef>

#include "ui/editor_widgets.h"  // EulerCache

namespace Engine {

struct EditorContext;

/**
 * @brief Editor bottom panel: the Animation editor.
 *
 * Drives keyframe editing for the selected entity's Animation component:
 * the timeline, keyframe table per track, easing pickers, and the live
 * preview of the resulting pose.
 *
 * Rendering/environment settings are NOT here: they live on the singleton
 * "Environment" entity and are edited in the Inspector (select it via the
 * Hierarchy's pinned Environment row). Editor/application preferences are in
 * the Preferences window (see PreferencesPanel). Frame timing and per-pass
 * profiling are surfaced via the viewport overlay and Tracy respectively.
 */
class BottomPanel {
    public:
        BottomPanel() = default;
        ~BottomPanel() = default;

        BottomPanel(const BottomPanel& other) = delete;
        BottomPanel& operator=(const BottomPanel& other) = delete;

        BottomPanel(BottomPanel && other) = delete;
        BottomPanel& operator=(BottomPanel && other) = delete;

    public:
        void draw(EditorContext& ec);

    private:
        void drawAnimationSection(EditorContext& ec);
        void drawShaderErrorsSection();
        void drawGpuProfilerSection();
        void drawRenderGraphSection(EditorContext& ec);

        // Timeline keyframe-dot drag state (Animation section).
        // m_animDotTrack: -1 none, 0 position, 1 rotation, 2 scale.
        // m_animDotIdx: index into the track being dragged. Time would
        // drift across frames under float math and lose the keyframe.
        int    m_animDotTrack = -1;
        size_t m_animDotIdx   = 0;

        // Euler-angle edit cache for the rotation keyframe editor, keyed by
        // keyframe index. See EulerCache for the gimbal-lock rationale.
        EulerCache<int> m_rotEulerCache;
};

} // namespace Engine
