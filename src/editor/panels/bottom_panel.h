#pragma once

#include <unordered_map>

#include <cstddef>

#include "panels/asset_browser_panel.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;
class EngineErrorLog;

/**
 * @brief Editor bottom panel: tabbed asset library, Animation editor and error log.
 *
 * The Assets tab is the Asset Browser, held here rather than floating: it is
 * the surface an author reaches for most often, and the bottom strip is the
 * one region wide enough for a grid that is not covering the viewport while
 * it is open. The Animation tab drives keyframe editing for the selected
 * entity's Animation component: the timeline, keyframe table per track,
 * easing pickers, and the live preview of the resulting pose. The Errors tab
 * lists recoverable engine failures from the editor-owned EngineErrorLog
 * (script-hook throws and any other reportError() source).
 *
 * Editor/application preferences live in the Preferences window (see
 * PreferencesPanel). Frame timing and per-pass profiling are surfaced via
 * the viewport overlay and Tracy respectively.
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
        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        void drawAnimationSection(EditorContext& ec, SceneIOController& sceneIO);
        void drawErrorsSection(EngineErrorLog& errorLog);

    private:
        // Held by value here rather than drawn by EditorSystem: the tab is the
        // panel, and what it remembers - chosen kind, tile size, the voice an
        // audition is running on - has to outlive the frames another tab is open.
        AssetBrowserPanel m_assets;

        // Timeline keyframe-dot drag state (Animation section).
        // m_animDotTrack: -1 none, 0 position, 1 rotation, 2 scale.
        // m_animDotIdx: index into the track being dragged. Time would
        // drift across frames under float math and lose the keyframe.
        int    m_animDotTrack = -1;
        size_t m_animDotIdx   = 0;

        /**
         * @brief One euler cache per rotation keyframe row.
         *
         * Not one shared cache: a track draws every keyframe in the same frame,
         * and EulerCache reseeds whenever its key changes - so a single instance
         * is reseeded by the next row before the dragged one is drawn again, and
         * the gimbal guard it exists to provide never survives a frame. The
         * inspector can hold one because it draws a single entity's row.
         */
        std::unordered_map<int, EulerCache<int>> m_rotEulerCaches;
};

} // namespace Vkm::Engine
