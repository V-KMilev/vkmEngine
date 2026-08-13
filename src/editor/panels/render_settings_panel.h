#pragma once

namespace Engine {

struct EditorContext;

/**
 * @brief Render Settings window: pass toggles + per-effect tuning.
 *
 * A floating, closeable window (Window > Render Settings) that edits the
 * RenderSystem's live RenderSettings - debug view, GTAO, bloom,
 * shadows and reflection probes - plus the VisibilitySystem culling
 * thresholds. Immediate-apply (no command stack); these are render/app
 * config, not scene edits.
 */
class RenderSettingsPanel {
    public:
        RenderSettingsPanel() = default;
        ~RenderSettingsPanel() = default;

        RenderSettingsPanel(const RenderSettingsPanel& other) = delete;
        RenderSettingsPanel& operator=(const RenderSettingsPanel& other) = delete;

        RenderSettingsPanel(RenderSettingsPanel && other) = delete;
        RenderSettingsPanel& operator=(RenderSettingsPanel && other) = delete;

    public:
        /**
         * @brief Draws the window while state.showRenderSettings is true; the
         * title-bar X clears it.
         */
        void draw(EditorContext& ec);

    private:
        bool m_confirmReset = false;  ///< Reset-to-defaults dialog intent.
};

} // namespace Engine
