#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/process/child_process.h"

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The editor's way to vkm: it runs `vkm new`, `vkm build` and `vkm package` and shows
 *        what they print in the Build window.
 *
 * vkm is the one implementation of each; the editor does none of them itself. A build
 * changes the module on disk, which EditorSystem's watch then reloads. A project opened
 * with no module is built once, so a new one runs its code without a terminal.
 */
class BuildController {
    public:
        BuildController() = default;
        ~BuildController() = default;

        BuildController(const BuildController& other) = delete;
        BuildController& operator=(const BuildController& other) = delete;

        BuildController(BuildController && other) = delete;
        BuildController& operator=(BuildController && other) = delete;

    public:
        /**
         * @brief vkm's launcher: at an SDK's root, or in tools/ in the engine's tree.
         *
         * @return Its path, or empty when this engine has none.
         */
        static std::filesystem::path launcher();

        /**
         * @brief Run `vkm` with @p args, ending a run still going.
         *
         * @param args What follows `vkm` on its command line.
         * @param openWhenDone A project to open once it succeeds, as after `vkm new`; empty for none.
         */
        void run(std::vector<std::string> args, std::string openWhenDone = {});

        /**
         * @brief Take what the run printed, finish it when it ends, and build a module-less project.
         *
         * @param state Takes EditorState::requestVkm, toasts, and the request to open a new project.
         * @param modulePath The module the open project loads, whose absence asks for a build.
         */
        void update(EditorState& state, const std::filesystem::path& modulePath);

        /**
         * @brief The Build window's contents: what ran, how it ended, and its output.
         *
         * @param state Asked to run a build or a package from the window's buttons.
         */
        void draw(EditorState& state);

        /**
         * @brief Whether nothing has run yet.
         *
         * @return True until the first run starts.
         */
        bool idle() const { return m_status == Status::Idle; }

        /**
         * @brief Whether the Build window should come to the front this frame.
         *
         * @return True once per run, as it starts.
         */
        bool takeReveal();

    private:
        enum class Status { Idle, Running, Succeeded, Failed, Stopped };

        /**
         * @brief Append output, line by line; a carriage return rewrites the line it ends.
         *
         * @param text As the program wrote it.
         */
        void append(const std::string& text);

    private:
        ChildProcess             m_process;
        Status                   m_status = Status::Idle;
        std::string              m_command;        ///< What ran, as typed
        std::string              m_openWhenDone;
        std::vector<std::string> m_lines;
        std::string              m_partial;        ///< The line still being written
        std::filesystem::path    m_checkedRoot;    ///< The project whose module was looked for
        bool                     m_reveal = false;
        bool                     m_follow = true;  ///< Jump to the output's end on the next frame
};

} // namespace Vkm::Engine
