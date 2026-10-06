#pragma once

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Render Settings window: pass toggles, per-effect tuning and culling thresholds.
 *
 * Edits the live RenderSettings immediately, outside the command stack; only Bake All
 * Probes edits the scene, as an undoable step.
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
         * @brief Draw the window while EditorState::showRenderSettings is set; the title-bar X clears it.
         *
         * @param ec Context whose RenderSettings are edited.
         */
        void draw(EditorContext& ec);

    private:
        bool m_confirmReset = false;  ///< Reset-to-defaults dialog is open.
};

} // namespace Vkm::Engine
