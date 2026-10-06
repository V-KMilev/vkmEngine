#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief A program run in the background, its output collected as it arrives.
 *
 * Standard output and error share one pipe, read on a thread of the process's own, so
 * a caller polls output() and finished() from its frame. stop() ends the program and
 * everything it started - a build's compilers included - so nothing outlives it.
 */
class ChildProcess {
    public:
        ChildProcess() = default;
        ~ChildProcess();

        ChildProcess(const ChildProcess& other) = delete;
        ChildProcess& operator=(const ChildProcess& other) = delete;

        ChildProcess(ChildProcess && other) = delete;
        ChildProcess& operator=(ChildProcess && other) = delete;

    public:
        /**
         * @brief Start @p program with @p args in @p workingDir, ending any program started before.
         *
         * @param program The executable, by path.
         * @param args Its arguments, each passed as one argument.
         * @param workingDir Where it runs; empty keeps this process's.
         * @return Whether it started; a failure is in output().
         */
        bool start(
            const std::filesystem::path& program,
            const std::vector<std::string>& args,
            const std::filesystem::path& workingDir = {}
        );

        /**
         * @brief End the program and everything it started, and wait for it.
         */
        void stop();

        /**
         * @brief Take the output that arrived since the last call.
         *
         * @return Text as the program wrote it, possibly ending mid-line.
         */
        std::string takeOutput();

        /**
         * @brief Whether a program was started and has not yet exited.
         *
         * @return True while it runs.
         */
        bool running();

        /**
         * @brief How the last program exited.
         *
         * @return Its exit code; -1 while it runs, when none started, or when it was stopped.
         */
        int exitCode();

    private:
        /// Reads the pipe until the program closes it, then reaps the program.
        void readUntilExit();

    private:
        std::mutex  m_mutex;
        std::string m_output;
        int         m_exitCode = -1;
        bool        m_running = false;
        std::thread m_reader;

#if defined(_WIN32)
        void* m_process = nullptr;
        void* m_job = nullptr;    ///< Holds the program and all it starts, so stop() ends them together
        void* m_pipe = nullptr;
#else
        int m_pid = 0;            ///< Also its process group's id, which holds all it starts
        int m_pipeFd = -1;
#endif
};

} // namespace Vkm::Engine
