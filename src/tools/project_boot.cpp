#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

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

namespace {

// Whether a log file can actually be created at @p path.
//
// Logger::init cannot answer this: it reports only whether a logger already
// existed, and its stream failing to open is silent - every later line turns
// into a "Failed to open log file" notice on stdout with the message itself
// dropped. So the probe happens here, before the logger is handed a path.
bool logFileWritable(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    return std::ofstream(path, std::ios::app).good();
}

} // namespace

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

    // Beside the project when the project can hold it: that is where a developer
    // looks, and a shipped game simply has no logs/ yet. An installed game's
    // directory is read-only, though, so the log falls back to the user's own
    // state directory rather than being lost - named after the project, because
    // one directory serves every game this engine ships.
    const std::filesystem::path root = ProjectPaths::projectRoot();
    std::filesystem::path logPath = root / "logs" / logFileName;
    if (!logFileWritable(logPath)) {
        logPath = ProjectPaths::userLogs() / root.filename() / logFileName;
        // Neither place will take it. Nothing can be logged, so stderr is the
        // only channel left to say why the host is not starting.
        if (!logFileWritable(logPath)) {
            std::fprintf(stderr, "vkm: cannot open a log file at %s\n",
                         logPath.string().c_str());
            return false;
        }
    }
    if (!Vkm::Log::Logger::init(logPath.string(), loggerTag, Vkm::Log::LogLevel::TRACE))
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
