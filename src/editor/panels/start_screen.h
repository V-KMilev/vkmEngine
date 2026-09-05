#pragma once

#include <string>
#include <vector>

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief What the editor shows when no project is open: a way in, and no
 *        pretence of a world.
 *
 * There is no viewport, hierarchy or asset browser behind this. An editor with
 * no project has nothing to put in them, and a workspace full of empty panels
 * says the opposite of what is true.
 *
 * So it is a way in and nothing else: New, Open, the projects opened before,
 * and the examples this engine ships. A recent entry carries the project's own
 * name rather than its directory's - a project is named in project.json and
 * that is what an author calls it - and says when it was last open and whether
 * it is still there, because a recents list whose rows lie about both is a list
 * nobody trusts twice.
 *
 * The names come off disk, so they are cached against the list they were read
 * from; whether a path still resolves is asked every frame, which is one stat
 * per row and keeps a folder deleted underneath it honest.
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
         * @param ec Editor context; the screen reads the recents list and asks
         *        for a project to be opened through it.
         */
        void draw(EditorContext& ec);

    private:
        /// One row: where the project is, and what it calls itself.
        struct Entry {
            std::string root;   ///< The path as it is stored, and as the row shows it.
            std::string name;   ///< Project::name, or the directory's if unreadable.
            std::string key;    ///< Same path resolved, so two spellings of one project compare equal.
        };

        /**
         * @brief Re-read the project names when the recents list has changed.
         *
         * @param roots The list this frame; compared against what was read.
         */
        void syncRecent(const std::vector<std::string>& roots);

        /// Read the examples shipped beside the engine, once per session.
        void scanExamples();

    private:
        std::vector<Entry>       m_recent;
        std::vector<std::string> m_recentRoots;   ///< What m_recent was built from.

        std::vector<Entry> m_examples;
        bool               m_examplesScanned = false;
};

} // namespace Vkm::Engine
