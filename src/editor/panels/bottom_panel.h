#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "framework/editor_panel.h"

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
 * display, keybinds) intentionally do NOT live here - they are in the
 * Preferences window (see PreferencesPanel).
 */
class BottomPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Bottom"; }
        void draw(EditorContext& ec) override;

    private:
        // "Rendering" detail = a preset bar + a tab bar (Lighting / Camera /
        // Effects / Scene / Pipeline). Each tab groups the EnvironmentConfig
        // knobs into collapsible cards with an enable toggle in the header.
        void drawRenderingSection(EditorContext& ec);
        void drawPresetBar(EditorContext& ec);
        void drawLightingTab(EditorContext& ec);
        void drawCameraTab(EditorContext& ec);
        void drawEffectsTab(EditorContext& ec);
        void drawSceneTab(EditorContext& ec);
        void drawPipelineTab(EditorContext& ec);

        void drawAnimationSection(EditorContext& ec);
        void drawStatisticsSection(EditorContext& ec);

        int m_selectedSection = 0;

        // Remembered values so a header toggle can switch an effect fully off
        // and back on without losing the user's tuning.
        std::string m_iblPathMemo;                 ///< restored when IBL re-enabled
        std::string m_lutPathMemo;                 ///< restored when grading re-enabled
        float       m_bloomStrengthMemo = 0.04f;   ///< restored when bloom re-enabled

        // Timeline keyframe-dot drag state (Animation section).
        // m_animDotTrack: -1 none, 0 position, 1 rotation, 2 scale.
        int   m_animDotTrack = -1;
        float m_animDotTime  = 0.0f;

        // Euler-angle edit cache for the rotation keyframe editor. Same
        // gimbal-lock guard as InspectorPanel: quaternion->Euler is singular
        // at +/-90 deg, so the edited Euler is the source of truth and is only
        // re-derived from the stored quat when that keyframe's rotation
        // changed outside the drag. m_rotEulerKey: keyframe index the cache is
        // valid for (-1 = none).
        int       m_rotEulerKey = -1;
        glm::vec3 m_rotEulerDeg{0.0f};

        struct ResourceCounts {
            size_t transforms = 0, meshes = 0, lights = 0, cameras = 0;
            size_t animations = 0, hierarchies = 0, names = 0;
            uint32_t animPlaying = 0, animPaused = 0;
            uint32_t lightsDir = 0, lightsPoint = 0, lightsSpot = 0, lightsDisabled = 0;
            float updateTimer = 0.0f;
        } m_resourceCounts;
};

} // namespace Engine
