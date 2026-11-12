#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "panels/console/console_view.h"
#include "panels/console/log_tail.h"

namespace Vkm::Engine {

class BuildController;
class EngineErrorLog;
struct EditorState;

/**
 * @brief The Console window: this session's log, the errors the project caused, and the build.
 *
 * The Log view follows the log file (LogTail), so it holds every line of the run without the logger
 * knowing it is read. The Errors view lists EngineErrorLog, a repeat counted, newest first.
 */
class ConsolePanel {
    public:
        ConsolePanel();
        ~ConsolePanel() = default;

        ConsolePanel(const ConsolePanel& other) = delete;
        ConsolePanel& operator=(const ConsolePanel& other) = delete;

        ConsolePanel(ConsolePanel && other) = delete;
        ConsolePanel& operator=(ConsolePanel && other) = delete;

    public:
        /// Take what was logged since the last frame; drawn or not, so nothing is missed.
        void poll() { m_followed |= m_tail.poll(); }

        /// Show @p view, as a build run does its own.
        void show(ConsoleView view) { m_view = view; }

        /// How many of the lines held are warnings.
        size_t warnings() const { return m_tail.counts()[static_cast<size_t>(Log::Level::Warning)]; }

        /**
         * @brief Draw the window's contents.
         *
         * Sets EditorState::consoleHoldsKeys while the log list has the keyboard.
         *
         * @param state  What the Build view runs against; told when the list keeps its keys.
         * @param errors What the Errors view lists; its Clear empties it.
         * @param build  What the Build view shows and runs.
         */
        void draw(EditorState& state, EngineErrorLog& errors, BuildController& build);

    private:
        static constexpr uint64_t NONE = std::numeric_limits<uint64_t>::max();  ///< m_selected with no line.

    private:
        /// Draw the Log view; the list's own keys, while it has the keyboard, set m_holdsKeys.
        void drawLog();
        void drawErrors(EngineErrorLog& errors);

        /// Refill m_visible when the lines, the levels shown or the search have changed.
        void filterLines();

        /// The selected line, or null when none is held any more.
        const LogLine* selectedLine() const;

    private:
        LogTail m_tail;

        std::array<bool, static_cast<size_t>(Log::Level::Count)> m_shown{};  ///< By level; trace, verbose
                                                                              ///< and debug start hidden.
        std::vector<size_t> m_visible;  ///< The lines shown, by index into LogTail::lines(), oldest first.

        // What m_visible was filtered from; a change to any refills it.
        uint64_t    m_filteredRevision = std::numeric_limits<uint64_t>::max();  ///< Max until the first fill.
        uint32_t    m_filteredLevels   = 0;
        std::string m_filteredSearch;

        uint64_t m_selected  = NONE;   ///< The selected line, by its number in the run (LogTail::dropped),
                                       ///< which outlives its place.
        bool     m_holdsKeys = false;  ///< The list had the keyboard and a line this frame.

        char        m_search[128] = {};
        bool        m_followed    = false;  ///< Lines arrived since the list was last drawn.
        bool        m_toEnd       = false;  ///< Scroll to the newest line on the next frame.
        size_t      m_unseen      = 0;      ///< Lines that arrived while scrolled up.
        ConsoleView m_view        = ConsoleView::Log;
};

} // namespace Vkm::Engine
