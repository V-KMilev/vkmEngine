#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "logger.h"

#include "debug/build_info.h"
#include "debug/engine_error_log.h"
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
    // moves the CWD, and absolute(argv[1]) would then answer differently.
    // Nothing is logged yet - the log file lives under the root being decided.
    bool argNotAProject = false;
    if (argc > 1) {
        const std::filesystem::path found = findProjectRoot(std::filesystem::absolute(argv[1], ec));
        if (found.empty()) argNotAProject = true;
        else               ProjectPaths::setProjectRoot(found);
    }

    // Pinned to the ENGINE root, because shaders load CWD-relative. Without it
    // a host only starts when it was launched from there.
    std::filesystem::current_path(ProjectPaths::engineRoot(), ec);

    const std::filesystem::path root = ProjectPaths::projectRoot();
    std::filesystem::path logPath = root / "logs" / logFileName;
    if (!logFileWritable(logPath)) {
        // Named after the project: one state directory serves every game this
        // engine ships.
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

SceneBootResult bootProjectScene(
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
            return {SceneBoot::Project, path.string()};
        }
        reportError("Scene", path.string(),
                    "entry scene failed to load; the default scene stands in");
        buildDefaultScene(scene, resources);
        return {SceneBoot::Failed, {}};
    }

    if (module.buildScene(scene)) {
        LOG_INFO("Scene built by the project's module");
        return {SceneBoot::Project, {}};
    }

    buildDefaultScene(scene, resources);
    LOG_INFO("Project '%s' supplies no scene of its own; opened the default scene",
             project.name.c_str());
    return {SceneBoot::Default, {}};
}

} // namespace Vkm::Engine
