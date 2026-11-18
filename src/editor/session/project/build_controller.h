#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "vkm_platform/process/child_process.h"

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief Runs `vkm new`, `vkm build` and `vkm package`, showing their output in the Console's Build view.
 *
 * vkm is the one implementation of each. A project opened with no module is built once, so a new one
 * runs its code without a terminal.
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
         * @brief Take what the run printed, finish it when it ends, and build a module-less project once.
         *
         * @param state Takes an ask of VkmRun, and toasts.
         * @param modulePath The module the open project loads, whose absence asks for a build.
         */
        void update(EditorState& state, const std::filesystem::path& modulePath);

        /**
         * @brief Draw the Console's Build view: what ran, how it ended, its output, and Stop.
         *
         * A run is asked for elsewhere: the menu bar's Build, File > Build Scripts or its key.
         */
        void draw();

        /// Bring the Console forward on this view the next frame, as a run does.
        void reveal() { m_reveal = true; }

        /**
         * @brief Whether the last run succeeded less than @p seconds ago.
         *
         * @param seconds How long a success is news.
         * @return True only within that time of the run's end.
         */
        bool succeededWithin(double seconds) const {
            return m_status == Status::Succeeded && ImGui::GetTime() - m_endedAt < seconds;
        }

        /**
         * @brief Whether nothing has run yet.
         *
         * @return True until the first run starts.
         */
        bool idle() const { return m_status == Status::Idle; }

        /// Whether the last run failed.
        bool failed() const { return m_status == Status::Failed; }

        /// Whether a run is under way.
        bool running() const { return m_status == Status::Running; }

        /**
         * @brief Whether the Console should come forward on its Build view this frame.
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
        Platform::ChildProcess   m_process;
        Status                   m_status  = Status::Idle;
        std::string              m_command;         ///< What ran, as typed.
        std::string              m_openWhenDone;
        std::vector<std::string> m_lines;
        std::string              m_partial;         ///< The line still being written.
        std::filesystem::path    m_checkedRoot;     ///< The project whose module was looked for.
        bool                     m_reveal  = false;
        bool                     m_follow  = true;  ///< Jump to the output's end on the next frame.
        double                   m_endedAt = 0.0;   ///< ImGui's clock when the last run ended.
};

} // namespace Vkm::Engine
