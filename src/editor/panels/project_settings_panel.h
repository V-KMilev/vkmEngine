#pragma once

#include <string>

#include "ui/asset_picker.h"
#include "io/project.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Project Settings window: what project.json records about the game.
 *
 * Edits go into EditorState::project and reach the file on Save. They bypass the command
 * stack, which is scene-scoped and dropped on scene load. The window says whether its
 * fields are unsaved, as nothing else would.
 */
class ProjectSettingsPanel {
    public:
        ProjectSettingsPanel() = default;
        ~ProjectSettingsPanel() = default;

        ProjectSettingsPanel(const ProjectSettingsPanel& other) = delete;
        ProjectSettingsPanel& operator=(const ProjectSettingsPanel& other) = delete;

        ProjectSettingsPanel(ProjectSettingsPanel && other) = delete;
        ProjectSettingsPanel& operator=(ProjectSettingsPanel && other) = delete;

    public:
        /**
         * @brief Draw the window while EditorState::showProjectSettings is set; the title-bar X clears it.
         *
         * @param ec Context whose EditorState::project is edited.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Whether the fields this window edits differ from what was written.
         *
         * Not the whole Project: Save also stamps the engine version and live RenderSettings,
         * which would read as unsaved after any quality slider moved.
         *
         * @param a As edited.
         * @param b As last written.
         * @return Whether every field this window shows is equal.
         */
        static bool sameEditedFields(const Project& a, const Project& b);

    private:
        AssetPicker m_scenePicker;

        /// The project as last written, and its root.
        Project     m_saved;
        std::string m_savedRoot;
};

} // namespace Vkm::Engine
