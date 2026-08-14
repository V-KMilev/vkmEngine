#pragma once

namespace Engine {

class Scene;
class ResourceManager;
class ScriptModule;
struct Project;

/**
 * @brief Which of the three sources supplied a booted scene.
 */
enum class SceneSource {
    Authored,   ///< Loaded from the project's entryScene.
    Generated,  ///< Built in code by the project's module.
    Default,    ///< The project supplied neither, so the default scene.
};

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
 * @return Which source supplied the scene.
 */
SceneSource bootProjectScene(const Project& project, ScriptModule& module,
                             Scene& scene, ResourceManager& resources);

} // namespace Engine
