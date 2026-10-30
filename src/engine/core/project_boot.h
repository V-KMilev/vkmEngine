#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

class BehaviorSystem;
class Clock;
class Engine;
class InputMap;
class NetSession;
class Scene;
class ResourceManager;
class ScriptModule;
struct Project;

/**
 * @brief Load the project's asset manifest and its gameplay module.
 *
 * Cooked assets only, no importers; call before any scene I/O. The runtime and the server
 * call it and treat a missing module as fatal; the editor opens a project without one and
 * builds it.
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
    Project,  ///< Its entry scene.
    Default,  ///< The project names no entry scene; the default scene stands in.
    Failed    ///< The named entry scene did not load.
};

/**
 * @brief Which world bootProjectWorld left standing, and the file it came from.
 */
struct SceneBootResult {
    SceneBoot source = SceneBoot::Default;

    std::string path;  ///< Absolute path the authored entry scene was read from. Empty otherwise, so a save
                       ///< asks for a name rather than writing over a load that failed.
};

/**
 * @brief Stand up the project's own world: its tick rate, its actions, its scene, and that scene's
 *        fingerprint.
 *
 * The scene is the authored entryScene, else the default scene. An entryScene that fails to load
 * falls through to the default scene, reported as Failed.
 *
 * @param project   The open project.
 * @param scene     Scene to fill; expected to be empty.
 * @param resources Resource manager the scene's assets are loaded into.
 * @param clock     Takes the project's tick rate.
 * @param input     Takes the project's actions, so a command's slots are known before a join.
 * @param net       Takes the world's fingerprint, for peers to compare (see fingerprintScene).
 * @return Which world is now in @p scene, and the file it was read from when it is the
 *         authored one.
 */
SceneBootResult bootProjectWorld(
    const Project& project,
    Scene& scene,
    ResourceManager& resources,
    Clock& clock,
    InputMap& input,
    NetSession& net
);

/**
 * @brief Stand up the project's own world in a host that plays it: the runtime, the server.
 *
 * bootProjectWorld into @p engine, refusing a world not the project's own, then the file a game's
 * restartScene() starts over while it has loaded no scene of its own.
 *
 * @param project   The open project.
 * @param engine    Its scene, resources, clock, input and net take the world.
 * @param behaviors Takes the file played (BehaviorSystem::setPlayedScene).
 * @param verb      "play", "serve" - for the message when there is no world.
 * @return False having logged that the project has no world of its own.
 */
bool bootPlayedWorld(const Project& project, Engine& engine, BehaviorSystem& behaviors, const char* verb);

} // namespace Vkm::Engine
