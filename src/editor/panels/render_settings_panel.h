#pragma once

#include "ui/file_picker.h"
#include "io/project/render_settings.h"

namespace Vkm::Engine {

template <typename T>
class Rows;

struct EditorContext;

/**
 * @brief Render Settings window: the project's RenderSettings, and a quality tier previewed over them.
 *
 * Edits the project's RenderSettings at once, outside the command stack, and writes them into
 * project.json as each edit ends (saveProjectRender); in a play session they are the game's. Only
 * Bake All Probes edits the scene, as an undoable step. A quality tier can be previewed over the
 * project's settings (Graphics::setQuality), which are not written meanwhile.
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
         * @param ec      Context whose RenderSettings are edited.
         * @param playing Whether a play session runs.
         */
        void draw(EditorContext& ec, bool playing);

    private:
        /// Write the settings into project.json once an edit to them has ended, outside a session.
        void keepInProject(EditorContext& ec, bool playing);

        /// The Preview Tier row: a tier shown over the project's settings, or none.
        void drawPreview(EditorContext& ec, bool playing);

        /// The Color Grading rows: the table, picked or brought in neutral, and its strength.
        void drawGrading(EditorContext& ec, Rows<RenderSettings>& rows);

    private:
        bool       m_confirmReset = false;  ///< Reset-to-defaults dialog is open.
        FilePicker m_gradingPicker;         ///< Picks RenderSettings::gradingLut.
};

} // namespace Vkm::Engine
