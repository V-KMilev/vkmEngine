#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/hash/fnv1a.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class ScriptModule;
struct Project;

/**
 * @brief Resolve the project, pin the working directory, and open the log file.
 *
 * The process prologue every host shares, and the order inside it is not
 * free to change - each step carries its reason where it stands in the .cpp.
 *
 * The working directory becomes the ENGINE root rather than the project's:
 * shaders load CWD-relative and ship with the engine, while everything a project
 * owns is addressed absolutely through ProjectPaths.
 *
 * The log goes in the project's logs/, and in ProjectPaths::userLogs() when the
 * project cannot hold one - an installed game's directory is read-only, and a
 * game that cannot log is a game nobody can diagnose.
 *
 * @param argc        Argument count, as main received it.
 * @param argv        Argument vector; argv[1], when given, names the project.
 * @param logFileName File name the log is written under, in whichever of the two
 *                    directories can hold it.
 * @param loggerTag   Tag every line this host logs is stamped with.
 * @return False when neither place will take a log file - fatal, so the host
 *         exits, and the reason goes to stderr because nothing can be logged.
 */
bool bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag);

/**
 * @brief Whether argv[1] names a project directory rather than a flag.
 *
 * The one place that decides it. bootHost consumes argv[1] when this is true,
 * and every host then walks the rest for flags of its own; none of them should
 * be re-deciding what argv[1] was, and bootHost should not be deciding it a
 * second way from the answer it hands out.
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
 * @brief Register the cooked asset factories and load the project's gameplay module.
 *
 * The two playing hosts - the runtime and the server - open a project the same
 * way: cooked factories only, no importers, then the asset library, then the
 * module that fills the behavior registry. Both need it before any scene I/O,
 * and both treat a missing or unloadable module as fatal where the editor only
 * warns (../reference/system/io.md, "What each host does when a project will
 * not open").
 *
 * @param module Receives the loaded module. Declare it before the Engine, so it
 *               outlives one: behaviors are destroyed during Engine teardown and
 *               their code must still be mapped then.
 * @param verb   What this host was about to do with the project - "playing",
 *               "serving" - for the message when there is no module to do it
 *               with.
 * @return False having logged the reason, which is the part that matters: built
 *         against another engine version, missing its entry, or unreadable.
 */
bool bootGameplayModule(ScriptModule& module, const char* verb);

/**
 * @brief Which world bootProjectScene left standing.
 *
 * There is always one, so this says whose it is rather than whether there is
 * any. The hosts read it apart on purpose: a game that opened the engine's
 * default scene has no world of its own to play, while the editor opening that
 * same scene is how a project with nothing in it yet starts.
 */
enum class SceneBoot {
    Project,  ///< The project's own world: its entry scene, or one its module built.
    Default,  ///< The project names no world of its own; the default scene stands in.
    Failed    ///< The project names an entry scene that did not load.
};

/**
 * @brief A hash of the scene file at @p path, or zero when there is no file.
 *
 * Both ends of a game must have loaded the same world: a slot is an entity's
 * name on the wire, and two ends whose scenes differ agree on every name and
 * mean different things by all of them. The schema fingerprint catches a
 * different build; this catches the same build with a stale scene, which is the
 * one a developer actually produces.
 *
 * Zero means "no file behind this world" - a generated world, or the default
 * scene standing in for a load that failed - and compares equal to nothing, so
 * such a host neither offers nor demands agreement.
 *
 * @param path Absolute path of the entry scene, as SceneBootResult reports it.
 * @return The hash, or zero.
 */
inline uint64_t fingerprintScene(const std::filesystem::path& path) {
    if (path.empty()) return 0;

    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;

    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    if (bytes.empty()) return 0;

    const uint64_t hash = fnv1a64(bytes.data(), bytes.size());
    return hash == 0 ? 1 : hash;  // zero is reserved for "no file"
}

/**
 * @brief Which world bootProjectScene left standing, and the file it came from.
 *
 * The path is stated rather than left to be re-derived because only one of the
 * three worlds has one, and the caller cannot tell which without repeating the
 * rule this function exists to hold. An editor that guesses wrong either offers
 * to overwrite a file it did not open, or calls a scene it read from disk
 * untitled and saves the next Ctrl+S somewhere the project never looks.
 */
struct SceneBootResult {
    SceneBoot   source = SceneBoot::Default;

    /**
     * @brief Absolute path the authored entry scene was read from.
     *
     * Empty for every other outcome, which is the whole point: a world a module
     * generated and the default scene standing in for a failed load are both
     * worlds with no file behind them, and a save must ask for a name rather
     * than write over the one that failed.
     */
    std::string path;
};

/**
 * @brief Put the project's own world into @p scene.
 *
 * The rule a project opens by, in one place because every host has to
 * agree on it: the authored entryScene, else the world the project's module
 * generates, else the default scene. **Exactly one** of them runs - seeding a
 * scene before asking the project leaves a stray camera, light and cube sitting
 * underneath whatever it then builds.
 *
 * An entryScene that fails to load falls through to the default scene rather
 * than leaving an empty world, and the return value is how a caller tells that
 * apart from a project that asked for nothing. Substituting the default scene
 * without saying so is what made a failed boot invisible to the shell.
 *
 * @param project The open project.
 * @param module The project's gameplay module, asked for a generated world.
 * @param scene Scene to fill; expected to be empty.
 * @param resources Resource manager the scene's assets are loaded into.
 * @return Which of the three worlds is now in @p scene, and the file it was
 *         read from when it is the authored one.
 */
SceneBootResult bootProjectScene(
    const Project& project,
    ScriptModule& module,
    Scene& scene,
    ResourceManager& resources
);

} // namespace Vkm::Engine
