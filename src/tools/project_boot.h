#pragma once

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class ScriptModule;
struct Project;

/**
 * @brief Resolve the project, pin the working directory, and open the log file.
 *
 * The process prologue all three hosts share. The order is the whole of it, and
 * it is not free to change: argv[1] is resolved against the launch directory, so
 * the project root has to be decided before current_path() moves that directory;
 * setProjectRoot has to run before anything composes a project path, because a
 * composed path is a plain string by then and will not follow a later override;
 * and the "that is not a project" warning has to wait for the logger, which is
 * the first thing able to report it.
 *
 * The working directory becomes the ENGINE root rather than the project's:
 * shaders load CWD-relative and ship with the engine, while everything a project
 * owns is addressed absolutely through ProjectPaths.
 *
 * This decides where a host reads and writes; bootProjectScene below decides
 * which world it then opens. Neither calls the other.
 *
 * @param argc        Argument count, as main received it.
 * @param argv        Argument vector; argv[1], when given, names the project.
 * @param logFileName File name inside the project's logs/ directory.
 * @param loggerTag   Tag every line this host logs is stamped with.
 * @return False when the log file cannot be opened - fatal, so the host exits.
 */
bool bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag);

/**
 * @brief Put the project's own world into @p scene.
 *
 * The rule a project opens by, in one place because all three hosts have to
 * agree on it: the authored entryScene, else the world the project's module
 * generates, else the default scene. **Exactly one** of them runs - seeding a
 * scene before asking the project leaves a stray camera, light and cube sitting
 * underneath whatever it then builds.
 *
 * An entryScene that fails to load falls through to the default scene rather
 * than leaving an empty world, and says so in the log.
 *
 * @param project The open project.
 * @param module The project's gameplay module, asked for a generated world.
 * @param scene Scene to fill; expected to be empty.
 * @param resources Resource manager the scene's assets are loaded into.
 */
void bootProjectScene(
    const Project& project,
    ScriptModule& module,
    Scene& scene,
    ResourceManager& resources
);

} // namespace Vkm::Engine
