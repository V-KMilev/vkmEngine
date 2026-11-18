#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "vkm_platform/process/file_lock.h"

namespace Vkm::Engine {

/**
 * @brief A host's log file, and its claim on it for as long as the host runs.
 */
struct LogClaim {
    std::string                       name;  ///< The file's name in its folder.
    std::optional<Platform::FileLock> lock;  ///< Empty when no name could be claimed.
};

/**
 * @brief Claim a log in @p folder for this host: @p fileName unless another live host holds it,
 *        then the first numbered name free (log-2.log, log-3.log, ...).
 *
 * So two hosts on one project - two editors - keep a log, and a Console, each. A claim ends with
 * its process, however that ends.
 *
 * @param folder   Where the log goes; it must exist.
 * @param fileName The first host's name for it.
 * @return The name and its claim; @p fileName unclaimed when none could be had.
 */
LogClaim claimLog(const std::filesystem::path& folder, const std::string& fileName);

/**
 * @brief Resolve the project, pin the working directory, and open the log file.
 *
 * The order of steps matters; the .cpp gives each reason. The working directory becomes the
 * ENGINE root (shaders load CWD-relative); project files are addressed through Roots. The
 * log goes in the project's logs/, else Roots::userLogs() (an installed game is read-only);
 * when neither opens, the run logs to the console only, and says so. VKM_INSTANCE names the log
 * outright (log-2.log); without it the host takes the first name no other host holds (claimLog).
 *
 * @param argc        Argument count, as main received it.
 * @param argv        Argument vector; argv[1], when given, names the project.
 * @param logFileName The log's file name, as the first host on a project takes it.
 * @param loggerTag   Tag every line this host logs is stamped with.
 */
void bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag);

/**
 * @brief Run a host's @p body, logging what escapes it as fatal.
 *
 * @param body The host's main from bootHost on, returning its exit status.
 * @return The body's status, or EXIT_FAILURE when it threw.
 */
int runHost(const std::function<int()>& body);

/**
 * @brief Whether argv[1] names a project directory rather than a flag.
 *
 * The one place that decides it; a host reads its own flags from firstFlagIndex.
 *
 * @param argc Argument count, as main received it.
 * @param argv Argument vector, as main received it.
 * @return Whether a project directory was given.
 */
inline bool projectGivenInArgs(int argc, char** argv) {
    return argc > 1 && argv[1][0] != '-';
}

/**
 * @brief Where a host's own flags begin, past the project directory.
 *
 * @param argc Argument count, as main received it.
 * @param argv Argument vector, as main received it.
 * @return The first index a host should read its own flags from.
 */
inline int firstFlagIndex(int argc, char** argv) {
    return projectGivenInArgs(argc, argv) ? 2 : 1;
}

/**
 * @brief Refuse anything past the project directory, for a host with no flags of its own.
 *
 * Call after @ref bootHost, which opens the log this writes to.
 *
 * @param argc Argument count, as main received it.
 * @param argv Argument vector, as main received it.
 * @param host Executable name, for the message.
 * @return True when there was nothing past the project directory.
 */
bool refuseExtraArgs(int argc, char** argv, const char* host);

} // namespace Vkm::Engine
