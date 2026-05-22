#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "ui/editor_widgets.h"  // EulerCache

namespace Engine {

struct EditorContext;

/**
 * @brief Editor bottom panel: the per-scene working surface.
 *
 * A grouped master-detail browser organised into TOOLS (Animation) and
 * INFO (Statistics); the selection fills the scrollable detail pane.
 *
 * Rendering/environment settings are NOT here: they live on the singleton
 * "Environment" entity and are edited in the Inspector (select it via the
 * Hierarchy's pinned Environment row). Editor/application preferences are in
 * the Preferences window (see PreferencesPanel).
 */
class BottomPanel {
    public:
        void draw(EditorContext& ec);

    private:
        void drawAnimationSection(EditorContext& ec);
        void drawStatisticsSection(EditorContext& ec);

        // Timeline keyframe-dot drag state (Animation section).
        // m_animDotTrack: -1 none, 0 position, 1 rotation, 2 scale.
        // m_animDotIdx: index into the track being dragged. Time would
        // drift across frames under float math and lose the keyframe.
        int    m_animDotTrack = -1;
        size_t m_animDotIdx   = 0;

        // Euler-angle edit cache for the rotation keyframe editor, keyed by
        // keyframe index. See EulerCache for the gimbal-lock rationale.
        EulerCache<int> m_rotEulerCache;

        struct ResourceCounts {
            size_t transforms = 0, meshes = 0, lights = 0, cameras = 0;
            size_t animations = 0, hierarchies = 0, names = 0;
            uint32_t animPlaying = 0, animPaused = 0;
            uint32_t lightsDir = 0, lightsPoint = 0, lightsSpot = 0, lightsDisabled = 0;
            float updateTimer = 0.0f;
        } m_resourceCounts;
};

} // namespace Engine
