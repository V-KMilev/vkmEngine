#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui/file_picker.h"
#include "io/project/project.h"

namespace Vkm::Platform {
class InputHandle;
} // namespace Vkm::Platform

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
         * @brief Whether the fields this window edits are as last written.
         *
         * Not the render block, which Render Settings writes into the file as it is edited.
         *
         * @param a As edited.
         * @param b As last written.
         * @return Whether every field this window shows is equal.
         */
        static bool sameEditedFields(const Project& a, const Project& b);

        /// The logos shown after the engine's, each with its picture and how long it holds.
        void drawSplash(Project& project);

        /// The project's own fonts, each a name a UI Text gives and its file.
        void drawFonts(Project& project);

        /// The loading screen's tips, the project's own screen over it, and whether the water shows.
        void drawLoading(Project& project);

        /**
         * @brief The game's input actions, each its bindings as chips, and the next key or button
         *        pressed added to one.
         *
         * @param project Whose actions are edited.
         * @param device  The window's keys and buttons, read past the editor's own UI.
         */
        void drawActions(Project& project, const Platform::InputHandle& device);

    private:
        FilePicker m_scenePicker;
        FilePicker m_imagePicker;
        FilePicker m_fontPicker;
        FilePicker m_screenPicker;

        size_t m_imageFor = 0;  ///< The logo a pick fills: an index into its list, or its size for a new one.
        size_t m_fontFor  = 0;  ///< The font a pick fills, the same way.

        size_t m_listeningFor = SIZE_MAX;  ///< The action whose "+" waits for the next key or button
                                           ///< pressed; none past the list's end.

        std::vector<bool> m_held;  ///< Which keys, then buttons, were down when last looked at, so the
                                   ///< next press is a new one.

        std::string m_newAction;  ///< What the action "Add" names.
};

} // namespace Vkm::Engine
