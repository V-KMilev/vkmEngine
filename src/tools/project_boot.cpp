#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <filesystem>

#include "logger.h"

#include "debug/build_info.h"
#include "ecs/scene.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"
#include "system/script/script_module.h"

#include "generator/default_scene.h"

namespace Vkm::Engine {

bool bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag) {
    std::error_code ec;

    // Resolved first, and against the launch directory: current_path() below
    // moves the CWD, and absolute(argv[1]) would then answer differently. The
    // root is also set before anything composes a project path, because a
    // composed path is a plain string by then and will not follow a later
    // override. Nothing is logged yet - the log file lives under the root being
    // decided.
    bool argNotAProject = false;
    if (argc > 1) {
        const std::filesystem::path found = findProjectRoot(std::filesystem::absolute(argv[1], ec));
        if (found.empty()) argNotAProject = true;
        else               ProjectPaths::setProjectRoot(found);
    }

    // Pin the working directory to the ENGINE root, not the project's: shaders
    // load CWD-relative ("shaders/forward/pbr") and ship with the engine, while
    // every project-owned path is absolute. Without this a host only starts when
    // launched from the engine root.
    std::filesystem::current_path(ProjectPaths::engineRoot(), ec);

    // A shipped game has no logs/ yet, and Logger::init fails if it cannot open
    // the file.
    const std::filesystem::path root = ProjectPaths::projectRoot();
    std::filesystem::create_directories(root / "logs", ec);
    if (!Vkm::Log::Logger::init((root / "logs" / logFileName).string(), loggerTag, Vkm::Log::LogLevel::TRACE))
        return false;

    // Deferred until the logger exists: a mistyped path would otherwise look
    // like it worked, but this is the first point anything can say so.
    if (argNotAProject) {
        LOG_WARNING("'%s' is not a project (no project.json in it or above it); "
                    "using the project beside this executable instead", argv[1]);
    }

    printBuildInfo();
    return true;
}

SceneBoot bootProjectScene(
    const Project& project,
    ScriptModule& module,
    Scene& scene,
    ResourceManager& resources
) {
    if (!project.entryScene.empty()) {
        const std::filesystem::path path =
            (ProjectPaths::projectRoot() / project.entryScene).lexically_normal();

        if (SceneSerializer::load(scene, resources, path.string())) {
            LOG_INFO("Opened scene '%s'", path.string().c_str());
            return SceneBoot::Project;
        }
        LOG_ERROR("Entry scene '%s' failed to load; the default scene stands in",
                  path.string().c_str());
        buildDefaultScene(scene, resources);
        return SceneBoot::Failed;
    }

    if (module.buildScene(scene)) {
        LOG_INFO("Scene built by the project's module");
        return SceneBoot::Project;
    }

    buildDefaultScene(scene, resources);
    LOG_INFO("Project '%s' supplies no scene of its own; opened the default scene",
             project.name.c_str());
    return SceneBoot::Default;
}

} // namespace Vkm::Engine
