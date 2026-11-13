#define VKM_LOG_CATEGORY "PROJECT"

#include "io/project/host_boot.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "vkm_log/logger.h"

#include "io/project/project.h"
#include "io/project/roots.h"
#include "vkm_platform/process/this_process.h"
#include "build_stamp.h"

namespace Vkm::Engine {

namespace {

// The most hosts on one project that keep a log each; one more shares the first's.
constexpr int MOST_LOGS = 16;

// This host's claim on its log, for as long as it runs.
std::optional<Platform::FileLock> g_logClaim;

/**
 * @brief Log the build banner: this binary, and each vkm module's hash.
 *
 * Reads the APP_* macros CMake defines and build_stamp.h's; call once at startup. The graphics
 * API's strings are logged by the backend, which has a device to ask.
 */
void printBuildInfo() {
    LOG_INFO_C("BUILD", "------- Build Information -------");
    LOG_INFO_C("BUILD", "Running '%s' v%s", APP_NAME, APP_VERSION);
    LOG_INFO_C("BUILD", "Build:  %s (%s)", APP_BUILD_DATE, APP_BRANCH);
    LOG_INFO_C("BUILD", "--------------- Debug -----------");
    LOG_INFO_C("BUILD", "vkmEngine:   %s @ %.8s", APP_VERSION,          APP_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmLog:      %s @ %.8s", VKM_LOG_VERSION,      VKM_LOG_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmPlatform: %s @ %.8s", VKM_PLATFORM_VERSION, VKM_PLATFORM_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmAudio:    %s @ %.8s", VKM_AUDIO_VERSION,    VKM_AUDIO_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmNetwork:  %s @ %.8s", VKM_NETWORK_VERSION,  VKM_NETWORK_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmPhysics:  %s @ %.8s", VKM_PHYSICS_VERSION,  VKM_PHYSICS_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmOpenGL:   %s @ %.8s", VKM_OPENGL_VERSION,   VKM_OPENGL_COMMIT_HASH);
    LOG_INFO_C("BUILD", "---------------------------------");
}

} // namespace

void bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag) {
    std::error_code ec;

    // Heard from here, not from the loop's first frame: a stop asked while the host boots (Ctrl+C,
    // SIGTERM) ends the run at that frame, where one before it was lost to a SIGINT its parent ignored.
    static_cast<void>(Platform::stopRequested());

    // A windowed host opens no terminal of its own; started from one, it still writes what it prints
    // there.
    Platform::attachParentConsole();

    // Resolved before current_path() below moves the CWD. Nothing is logged yet: the log file
    // lives under the root being decided.
    bool argNotAProject = false;
    if (projectGivenInArgs(argc, argv)) {
        const std::filesystem::path found = findProjectRoot(std::filesystem::absolute(argv[1], ec));
        if (found.empty()) argNotAProject = true;
        else               Roots::setProjectRoot(found);
    }

    // Pinned to the ENGINE root, because shaders load CWD-relative.
    std::filesystem::current_path(Roots::engineRoot(), ec);

    const std::filesystem::path root = Roots::projectRoot();
    // Two hosts on one project keep a log each: VKM_INSTANCE names it, or the first free is claimed.
    std::string fileName = logFileName;
    if (const char* instance = std::getenv("VKM_INSTANCE"); instance && *instance) {
        const std::filesystem::path named(fileName);
        fileName = named.stem().string() + "-" + instance + named.extension().string();
    } else {
        std::filesystem::create_directories(root / "logs", ec);
        LogClaim claim = claimLog(root / "logs", fileName);
        fileName   = std::move(claim.name);
        g_logClaim = std::move(claim.lock);
    }

    // The user's is named after the project: one state directory serves every game.
    const std::filesystem::path projectLog = root / "logs" / fileName;
    const std::filesystem::path userLog    = Roots::userLogs() / root.filename() / fileName;
    if (!Log::Logger::init(projectLog, loggerTag, Log::Level::Trace)
        && !Log::Logger::init(userLog, loggerTag, Log::Level::Trace)) {
        LOG_WARNING(
            "No log file opens at %s or at %s; this run logs to the console only",
            projectLog.string().c_str(),
            userLog.string().c_str()
        );
    }

    // Deferred until the logger exists, the first point anything can be said.
    if (argNotAProject) {
        LOG_WARNING(
            "'%s' is not a project (no project.json in it or above it); "
            "using the project beside this executable instead",
            argv[1]
        );
    }

    printBuildInfo();
}

LogClaim claimLog(const std::filesystem::path& folder, const std::string& fileName) {
    // A folder no file can be made in (an installed game's) claims nothing, and says nothing here:
    // bootHost goes on to the user's folder.
    if (!std::ofstream(folder / fileName, std::ios::app)) return {fileName, std::nullopt};
    const std::filesystem::path named(fileName);
    for (int n = 1; n <= MOST_LOGS; ++n) {
        const std::string name = n == 1
            ? fileName
            : named.stem().string() + "-" + std::to_string(n) + named.extension().string();
        Platform::FileLock lock(folder / name);
        if (lock.held()) return {name, std::move(lock)};
    }
    return {fileName, std::nullopt};
}

int runHost(const std::function<int()>& body) {
    try {
        return body();
    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }
    return EXIT_FAILURE;
}

bool refuseExtraArgs(int argc, char** argv, const char* host) {
    const int extra = firstFlagIndex(argc, argv);
    if (extra >= argc) return true;

    LOG_ERROR("%s takes a project directory and nothing else, and '%s' is neither", host, argv[extra]);
    LOG_ERROR("Run it as: %s [project-directory]", host);
    return false;
}

} // namespace Vkm::Engine
