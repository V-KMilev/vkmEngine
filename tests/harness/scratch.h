#pragma once

// What every test has: a directory of its own, the log it wrote, and a way to say it cannot run here.

#include <cstdint>
#include <filesystem>
#include <string>

namespace Vkm::Test {

/**
 * @brief Thrown by skip(): the test cannot run on this machine, which ctest shows as skipped (exit 77)
 *        unless a check failed before it.
 */
struct Skip {
    std::string reason;
};

/// Stop the test as skipped, saying why: a missing device, never a failing check; one that failed
/// before it still fails the test.
[[noreturn]] inline void skip(const std::string& reason) { throw Skip{reason}; }

/// The build tree the running test binary is in: the folder above its bin/, found at run time so a
/// binary built on one system runs under another's paths.
std::filesystem::path buildDir();

/**
 * @brief This test's own directory under the build tree, empty when it starts.
 *
 * Kept after a failure, for what it wrote; removed after a pass. $XDG_CONFIG_HOME and
 * $XDG_STATE_HOME point at its `home` folder, so a player's files never reach the user's.
 */
const std::filesystem::path& scratch();

/// Everything logged at WARNING and above so far in this test, and at INFO in a TEST_WITH_INFO_LOG.
std::string logText();

/// How many times @p text appears in the log so far.
int countInLog(const std::string& text);

/**
 * @brief Write @p text to @p path, making its folders.
 *
 * @param path Where.
 * @param text What, as bytes.
 */
void writeText(const std::filesystem::path& path, const std::string& text);

/// The bytes of @p path; empty when it will not open.
std::string readText(const std::filesystem::path& path);

/// How many times the process has called operator new so far, in any form, on any thread: a hot path
/// that allocates nothing leaves it where it was. It skips the test on Windows, where a DLL's own calls
/// are not counted, and under a sanitizer, which brings its own operator new.
uint64_t allocations();

} // namespace Vkm::Test
