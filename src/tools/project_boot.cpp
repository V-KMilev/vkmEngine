#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <fstream>

#include "logger.h"

#include "core/clock.h"
#include "debug/build_info.h"
#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "io/project.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "net/net_session.h"
#include "resource/resource_manager.h"
#include "platform/library/dynamic_library.h"
#include "system/script/script_module.h"

#include "resource/generate/default_scene.h"

namespace Vkm::Engine {

namespace {

// Whether a log file can be created at @p path. Logger::init cannot say: a stream that fails to
// open is silent there, and every later line is dropped.
bool logFileWritable(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    return std::ofstream(path, std::ios::app).good();
}

} // namespace

bool bootHost(int argc, char** argv, const char* logFileName, const char* loggerTag) {
    std::error_code ec;

    // Resolved before current_path() below moves the CWD. Nothing is logged yet: the log file
    // lives under the root being decided.
    bool argNotAProject = false;
    if (projectGivenInArgs(argc, argv)) {
        const std::filesystem::path found = findProjectRoot(std::filesystem::absolute(argv[1], ec));
        if (found.empty()) argNotAProject = true;
        else               ProjectPaths::setProjectRoot(found);
    }

    // Pinned to the ENGINE root, because shaders load CWD-relative.
    std::filesystem::current_path(ProjectPaths::engineRoot(), ec);

    const std::filesystem::path root = ProjectPaths::projectRoot();
    // VKM_LOG_SUFFIX keeps two copies of a host on one project out of one file.
    std::string fileName = logFileName;
    if (const char* suffix = std::getenv("VKM_LOG_SUFFIX"); suffix && *suffix) {
        const std::filesystem::path named(fileName);
        fileName = named.stem().string() + "-" + suffix + named.extension().string();
    }

    std::filesystem::path logPath = root / "logs" / fileName;
    if (!logFileWritable(logPath)) {
        // Named after the project: one state directory serves every game.
        logPath = ProjectPaths::userLogs() / root.filename() / fileName;
        // Neither place will take it; stderr is the only channel left.
        if (!logFileWritable(logPath)) {
            std::fprintf(stderr, "vkm: cannot open a log file at %s\n", logPath.string().c_str());
            return false;
        }
    }
    if (!Vkm::Log::Logger::init(logPath.string(), loggerTag, Vkm::Log::LogLevel::TRACE))
        return false;

    // Deferred until the logger exists, the first point anything can be said.
    if (argNotAProject) {
        LOG_WARNING(
            "'%s' is not a project (no project.json in it or above it); "
            "using the project beside this executable instead",
            argv[1]
        );
    }

    printBuildInfo();
    return true;
}

SceneBootResult bootProjectWorld(
    const Project& project,
    ScriptModule& module,
    Scene& scene,
    ResourceManager& resources,
    Clock& clock,
    NetSession& net
) {
    // Before anything in the project ticks; one editor session can open a second project.
    clock.setTickRate(project.tickRate);

    SceneBootResult boot;
    if (!project.entryScene.empty()) {
        const std::filesystem::path path = ProjectPaths::resolveProjectPath(project.entryScene);

        if (SceneSerializer::load(scene, resources, path.string())) {
            LOG_INFO("Opened scene '%s'", path.string().c_str());
            boot = {SceneBoot::Project, path.string()};
        } else {
            reportError("Scene", path.string(), "entry scene failed to load; the default scene stands in");
            buildDefaultScene(scene, resources);
            boot = {SceneBoot::Failed, {}};
        }
    } else if (module.buildScene(scene, resources)) {
        LOG_INFO("Scene built by the project's module");
        boot = {SceneBoot::Project, {}};
    } else {
        buildDefaultScene(scene, resources);
        LOG_INFO("Project '%s' supplies no scene of its own; opened the default scene", project.name.c_str());
    }

    // Zero, which agrees with nothing, unless an authored file stands behind the world.
    net.setWorld(fingerprintScene(boot.path));
    return boot;
}

bool refuseExtraArgs(int argc, char** argv, const char* host) {
    const int extra = firstFlagIndex(argc, argv);
    if (extra >= argc) return true;

    LOG_ERROR("%s takes a project directory and nothing else, and '%s' is neither", host, argv[extra]);
    LOG_ERROR("Run it as: %s [project-directory]", host);
    return false;
}

std::filesystem::path gameplayModulePath() {
    return ProjectPaths::projectBin() / DynamicLibrary::platformName("game");
}

bool bootGameplayModule(ScriptModule& module, const char* verb) {
    std::error_code ec;

    // Cooked assets only: no recipe import is installed. Must precede scene I/O.
    AssetLibrary::get().load(AssetLibrary::Truth::Manifest);

    const std::filesystem::path modulePath = gameplayModulePath();
    if (!std::filesystem::exists(modulePath, ec)) {
        LOG_ERROR(
            "No gameplay module at '%s' - build the project before %s it",
            modulePath.string().c_str(),
            verb
        );
        return false;
    }
    // No bus: the Engine that owns one does not exist yet, nor a previous module.
    if (!module.load(modulePath, nullptr)) {
        LOG_ERROR("Gameplay module '%s' did not load", modulePath.string().c_str());
        return false;
    }
    return true;
}

} // namespace Vkm::Engine
