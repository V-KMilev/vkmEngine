#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "undo/command_host.h"
#include "panels/console/console_view.h"

namespace Vkm::Engine {

/**
 * @brief One transient message to the author.
 */
struct Toast {
    ToastKind                  kind      = ToastKind::Info;
    std::string                message;
    float                      remaining = 0.0f;  ///< Seconds it still shows.
    std::optional<ConsoleView> view;              ///< The Console view its message points at, if any.

    /**
     * @brief The Console view a click opens: its message's, else the Log for an error or a warning.
     *
     * @return None for an info toast that points at no view: a click only puts it away.
     */
    std::optional<ConsoleView> opens() const {
        if (view) return view;
        if (kind == ToastKind::Error || kind == ToastKind::Warning) return ConsoleView::Log;
        return std::nullopt;
    }
};

/**
 * @brief What the editor said lately, oldest first, shown above the status bar.
 *
 * They wait while the pointer is over them; a click puts one away.
 */
class Toasts {
    public:
        static constexpr size_t MAX_SHOWN = 3;  ///< The most shown at once; the oldest goes for a newer one.

        Toasts() = default;
        ~Toasts() = default;

        Toasts(const Toasts& other) = delete;
        Toasts& operator=(const Toasts& other) = delete;

        Toasts(Toasts && other) = delete;
        Toasts& operator=(Toasts && other) = delete;

    public:
        /**
         * @brief Show @p message; the newest said again only shows for longer.
         *
         * @param kind    How loudly.
         * @param message What to say; " - see the Console's" and @p view's name are appended to it.
         * @param view    The view a click opens (Toast::opens); none appends nothing and takes it from
         *                @p kind.
         * @param seconds How long; 0 or less takes the length from @p kind.
         */
        void push(
            ToastKind kind,
            std::string message,
            std::optional<ConsoleView> view = std::nullopt,
            float seconds = 0.0f
        ) {
            if (view) message += std::string(" - see the Console's ") + consoleViewName(*view);
            if (seconds <= 0.0f) {
                seconds = (kind == ToastKind::Error) ? 6.0f : (kind == ToastKind::Warning) ? 4.0f : 2.5f;
            }
            if (!m_shown.empty() && m_shown.back().kind == kind && m_shown.back().message == message) {
                m_shown.back().remaining = seconds;
                m_shown.back().view      = view;
                return;
            }
            m_shown.push_back({kind, std::move(message), seconds, view});
            if (m_shown.size() > MAX_SHOWN) m_shown.erase(m_shown.begin());
        }

        /**
         * @brief Count them down by @p deltaTime, unless the pointer was over them, and draw them.
         *
         * @return The Console view a click asked for, if any.
         */
        std::optional<ConsoleView> draw(float deltaTime);

        const std::vector<Toast>& shown() const { return m_shown; }

    private:
        std::vector<Toast> m_shown;
        bool               m_hovered = false;  ///< Last frame's: while hovered, they wait.
};

} // namespace Vkm::Engine
