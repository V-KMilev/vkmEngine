#pragma once

// The engine's executables, run as a user runs them, with no display to open a window on.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "vkm_platform/process/child_process.h"

namespace Vkm::Test {

/**
 * @brief How a run of a host ended.
 */
struct Ran {
    bool        ended = false;  ///< False when it was still running at the deadline, and was stopped.
    int         exit  = -1;
    std::string output;         ///< What it printed, its log lines among it.
};

/// The host @p name ("vkm_cook", "vkm_server", "vkm_runtime") as built.
inline std::filesystem::path hostPath(const std::string& name) {
    return std::filesystem::path(VKM_TEST_HOST_DIR) / (name + VKM_TEST_HOST_SUFFIX);
}

/// Take the display away from what this test starts: a host that would open a window cannot.
inline void noDisplay() {
#if !defined(_WIN32)
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
#endif
}

/**
 * @brief Run @p program with @p args to its end.
 *
 * @param seconds How long before it is taken to have hung, and stopped: a deadline, never a measure.
 */
inline Ran runProgram(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    double seconds = 300.0
) {
    noDisplay();
    Vkm::Platform::ChildProcess process;
    Ran ran;
    if (!process.start(program, args)) {
        ran.output = process.takeOutput();
        return ran;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
        ran.output += process.takeOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ran.ended = !process.running();
    process.stop();
    ran.output += process.takeOutput();
    ran.exit = process.exitCode();
    return ran;
}

/// Run the host @p name with @p args to its end, as runProgram does.
inline Ran runHost(const std::string& name, const std::vector<std::string>& args) {
    return runProgram(hostPath(name), args);
}

/// How many times @p text is in @p output.
inline int countIn(const std::string& output, const std::string& text) {
    int count = 0;
    for (size_t at = output.find(text); at != std::string::npos; at = output.find(
        text,
        at + text.size()
    )) ++count;
    return count;
}

} // namespace Vkm::Test
