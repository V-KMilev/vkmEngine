#pragma once

#include <string>
#include <vector>

namespace Vkm::Engine {

struct EditorContext;
struct EditorState;

/**
 * @brief What the editor shows when no project is open: the projects you have, and the examples.
 *
 * One panel over a dimmed sky. Projects lists the recents, newest first, flags one made
 * for an engine this one cannot build, and offers to move it; Examples offers a copy of
 * each shipped example through the New Project dialog, so the original stays as shipped.
 */
class StartScreen {
    public:
        StartScreen() = default;
        ~StartScreen() = default;

        StartScreen(const StartScreen& other) = delete;
        StartScreen& operator=(const StartScreen& other) = delete;

        StartScreen(StartScreen && other) = delete;
        StartScreen& operator=(StartScreen && other) = delete;

    public:
        /**
         * @brief Draw the screen into the region the caller has already sized.
         *
         * @param ec Supplies the recents list and opens the chosen project.
         */
        void draw(EditorContext& ec);

    private:
        /// One project as the screen shows it, read from its project.json.
        struct Entry {
            std::string root;            ///< As stored and shown.
            std::string key;             ///< Resolved, so two spellings compare equal.
            std::string name;            ///< Project::name, or the directory's if unreadable.
            std::string description;     ///< Project::description.
            std::string engineVersion;   ///< Project::engineVersion.
        };

        /**
         * @brief Re-read the projects when the recents list has changed.
         *
         * @param roots This frame's list.
         */
        void syncRecent(const std::vector<std::string>& roots);

        /// Read the shipped examples, once per session.
        void scanExamples();

        /**
         * @brief The header: the engine's mark and version, and New Project and Open.
         *
         * @param ec Supplies the render seam for the mark, and the state the buttons set.
         * @param width The panel's inner width.
         */
        void drawHeader(EditorContext& ec, float width);

        /**
         * @brief The Projects tab: a filter and the recents, or what to do without any.
         *
         * @param state Opens a project, or removes one from the recents.
         */
        void drawProjects(EditorState& state);

        /**
         * @brief The Examples tab: a card per example, each making a copy.
         *
         * @param state Receives the New Project request.
         */
        void drawExamples(EditorState& state);

        /**
         * @brief The question a project made for another engine raises before it opens.
         *
         * @param state Opens the project once it is moved.
         */
        void drawMoveDialog(EditorState& state);

    private:
        std::vector<Entry>       m_recent;
        std::vector<std::string> m_recentRoots;   ///< What m_recent was built from.

        std::vector<Entry> m_examples;
        bool               m_examplesScanned = false;

        char  m_filter[128] = {};
        bool  m_showExamples = false;   ///< Selects the Examples tab on the next frame.
        Entry m_moving;                 ///< The project the move dialog asks about.
        bool  m_moveOpen = false;
};

} // namespace Vkm::Engine
