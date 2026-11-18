#pragma once

namespace Vkm::Engine {

/**
 * @brief What the Console shows.
 */
enum class ConsoleView {
    Log,     ///< The session's log.
    Errors,  ///< The failures the project caused.
    Build    ///< The vkm command running or last run.
};

/**
 * @brief The word the Console's tab gives @p view, for a toast pointing at it.
 *
 * @param view The view named.
 * @return Its tab's word.
 */
inline const char* consoleViewName(ConsoleView view) {
    switch (view) {
        case ConsoleView::Log:    return "Log";
        case ConsoleView::Errors: return "Errors";
        case ConsoleView::Build:  return "Build";
    }
    return "Log";
}

} // namespace Vkm::Engine
