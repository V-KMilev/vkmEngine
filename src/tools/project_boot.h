#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/fnv1a.h"

namespace Vkm::Engine {

class Clock;
class NetSession;
class Scene;
class ResourceManager;
class ScriptModule;
struct Project;

/**
 * @brief Resolve the project, pin the working directory, and open the log file.
 *
 * The order of steps matters; the .cpp gives each reason. The working directory becomes the
 * ENGINE root (shaders load CWD-relative); project files are addressed through ProjectPaths. The
 * log goes in the project's logs/, else ProjectPaths::userLogs() (an installed game is read-only).
 *
 * @param argc        Argument count, as main received it.
 * @param argv        Argument vector; argv[1], when given, names the project.
 * @param logFileName The log's file name.
 * @param loggerTag   Tag every line this host logs is stamped with.
 * @return False, fatally, when neither place takes a log file (reason on stderr) or a logger
 *         already exists.
 */
bool bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag);

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

/**
 * @brief Where the open project's build writes its gameplay module.
 *
 * @return The module's path in the project's bin/, named for this platform.
 */
std::filesystem::path gameplayModulePath();

/**
 * @brief Load the project's asset manifest and its gameplay module.
 *
 * Cooked assets only, no importers; call before any scene I/O. A host treats a missing
 * module as fatal (docs/reference/io.md).
 *
 * @param module Receives the loaded module. Declare it before the Engine so it outlives it:
 *               behaviors are destroyed in Engine teardown and their code must stay mapped.
 * @param verb   "playing", "serving" - for the message when there is no module.
 * @return False having logged the reason: no module built, built against
 *         another engine version, missing its entry, or unreadable.
 */
bool bootGameplayModule(ScriptModule& module, const char* verb);

/**
 * @brief Which world bootProjectWorld left standing.
 *
 * There is always one. Default is not Project: no world of the project's own to play, but where
 * an empty project starts.
 */
enum class SceneBoot {
    Project,  ///< Its entry scene, or one its module built.
    Default,  ///< The project names no world; the default scene stands in.
    Failed    ///< The named entry scene did not load.
};

/**
 * @brief A hash of the scene file at @p path, line ends aside, or zero when there
 *        is no file.
 *
 * Both ends of a game must load the same world, as a slot is an entity's name on the wire; this
 * catches a stale scene. Zero (no file behind the world) compares equal to nothing, so such a host
 * neither offers nor demands agreement.
 *
 * @param path Absolute path of the entry scene, as SceneBootResult reports it.
 * @return The hash, or zero.
 */
inline uint64_t fingerprintScene(const std::filesystem::path& path) {
    if (path.empty()) return 0;

    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return 0;

    // Without '\r': a Windows checkout may hold CRLF, and in JSON '\r' is only whitespace.
    bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());

    const uint64_t hash = fnv1a64Bytes(bytes.data(), bytes.size());
    return hash == 0 ? 1 : hash;  // zero is reserved for "no file"
}

/**
 * @brief Which world bootProjectWorld left standing, and the file it came from.
 */
struct SceneBootResult {
    SceneBoot source = SceneBoot::Default;

    /**
     * @brief Absolute path the authored entry scene was read from.
     *
     * Empty otherwise, so a save asks for a name rather than writing over a load that failed.
     */
    std::string path;
};

/**
 * @brief Stand up the project's own world: its tick rate, its scene, and that scene's fingerprint.
 *
 * The scene is the authored entryScene, else the module's generated world, else the default
 * scene. An entryScene that fails to load falls through to the default scene, reported as Failed.
 *
 * @param project   The open project.
 * @param module    The project's gameplay module, asked for a generated world.
 * @param scene     Scene to fill; expected to be empty.
 * @param resources Resource manager the scene's assets are loaded into.
 * @param clock     Takes the project's tick rate.
 * @param net       Takes the world's fingerprint, for peers to compare (see fingerprintScene).
 * @return Which of the three worlds is now in @p scene, and the file it was
 *         read from when it is the authored one.
 */
SceneBootResult bootProjectWorld(
    const Project& project,
    ScriptModule& module,
    Scene& scene,
    ResourceManager& resources,
    Clock& clock,
    NetSession& net
);

} // namespace Vkm::Engine
