#pragma once

#include <filesystem>
#include <optional>

#include "system/render/render_settings.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Render Settings window: pass toggles, per-effect tuning and culling thresholds.
 *
 * Edits the live RenderSettings immediately, outside the command stack; only Bake All
 * Probes edits the scene, as an undoable step. A quality tier can be previewed over the
 * project's settings (withPreset), which are held aside meanwhile and not written.
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
         * Outside a play session the settings are the project's, and an edit is written into its
         * project.json as it ends (saveProjectRender); in one they are the game's to change.
         *
         * @param ec      Context whose RenderSettings are edited.
         * @param playing Whether a play session runs.
         */
        void draw(EditorContext& ec, bool playing);

    private:
        /// Write the settings into project.json once an edit to them has ended, outside a session.
        void keepInProject(EditorContext& ec, bool playing);

        /// The Preview Tier row: a tier shown over the project's settings, or none.
        void drawPreview(EditorContext& ec, bool playing);

        /// Put the project's own settings back over a tier's preview.
        void endPreview(EditorContext& ec);

    private:
        bool m_confirmReset = false;  ///< Reset-to-defaults dialog is open.

        RenderSettings        m_saved;               ///< What project.json was last written with.
        std::filesystem::path m_savedRoot;           ///< The project it was written to.
        bool                  m_savedKnown = false;  ///< Whether m_saved holds anything yet.

        /// The tier shown in place of the project's settings, which EditorState::renderPreviewHeld
        /// holds meanwhile; none once they are back.
        std::optional<GraphicsPreset> m_preview;
};

} // namespace Vkm::Engine
