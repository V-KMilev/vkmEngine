#pragma once

#include <string>

#include "framework/asset_picker.h"
#include "io/project.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief Project Settings window: what project.json records about the game.
 *
 * A floating, closeable window (File > Project > Settings...) over the fields
 * of Project - its name, the scene it boots, its tick rate, and how many
 * players it is for and on what port. Those last two had no surface at all: a
 * project author edited JSON by hand for a setting the engine reads on every
 * run.
 *
 * Edits go into EditorState::project and reach the file on Save, not on
 * keystroke - this writes to disk, and every other file the editor writes is
 * written when someone says so. Not on the command stack either: that history
 * is scene-scoped and thrown away by the next scene load, so an undo of a
 * project-level change would silently do nothing.
 *
 * Which is why the window says whether what it shows has been written yet. A
 * field edited here and left unsaved looks exactly like one that was saved,
 * and the next thing to read project.json - a runtime, a server, a cook - reads
 * the file rather than what is on screen.
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
         * @brief Draws the window while state.showProjectSettings is true; the
         * title-bar X clears it.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Whether the fields this window edits differ from what was written.
         *
         * The fields, not the whole Project: Save also stamps the engine version
         * and takes the render settings live off the render system, so a
         * comparison of everything would read as unsaved from the first frame
         * anybody moved a quality slider.
         *
         * @param a One project.
         * @param b The other.
         * @return Whether every field this window shows is equal.
         */
        static bool sameEditedFields(const Project& a, const Project& b);

    private:
        AssetPicker m_scenePicker;

        /// The project as it was last known written, and the root it belongs to.
        Project     m_saved;
        std::string m_savedRoot;
};

} // namespace Vkm::Engine
