#pragma once

#include <cstddef>
#include <cstdint>

#include "../editor_panel.h"

namespace Engine {

struct EditorContext;

/**
 * @brief Editor bottom panel: the per-scene working surface.
 *
 * A grouped master-detail browser. The left list is organised into
 * WORLD (Environment, Rendering), TOOLS (Animation) and INFO (Statistics);
 * the selection fills the scrollable detail pane on the right.
 *
 * Editor/application preferences (camera tuning, gizmo snap defaults,
 * display, keybinds) intentionally do NOT live here -- they are in the
 * Preferences window (see PreferencesPanel).
 */
class BottomPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Bottom"; }
        void draw(EditorContext& ec) override;

    private:
        void drawEnvironmentSection(EditorContext& ec);
        void drawRenderingSection(EditorContext& ec);
        void drawAnimationSection(EditorContext& ec);
        void drawStatisticsSection(EditorContext& ec);

        int m_selectedSection = 0;

        // Timeline keyframe-dot drag state (Animation section).
        // m_animDotTrack: -1 none, 0 position, 1 rotation, 2 scale.
        int   m_animDotTrack = -1;
        float m_animDotTime  = 0.0f;

        struct ResourceCounts {
            size_t transforms = 0, meshes = 0, lights = 0, cameras = 0;
            size_t animations = 0, hierarchies = 0, names = 0;
            uint32_t animPlaying = 0, animPaused = 0;
            uint32_t lightsDir = 0, lightsPoint = 0, lightsSpot = 0, lightsDisabled = 0;
            float updateTimer = 0.0f;
        } m_resourceCounts;
};

} // namespace Engine
