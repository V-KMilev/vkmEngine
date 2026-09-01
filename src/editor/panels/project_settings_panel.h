#pragma once

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
};

} // namespace Vkm::Engine
