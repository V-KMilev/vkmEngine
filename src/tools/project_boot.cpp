#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <fstream>

#include "logger.h"

#include "debug/build_info.h"
#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "io/project.h"
#include "asset_registration.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"
#include "platform/library/dynamic_library.h"
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
    // A second copy of the same host, playing the same project, would otherwise
    // interleave its lines into the first one's file - which is what two
    // clients against one server looks like, and the case where the log matters
    // most. VKM_LOG_SUFFIX names them apart; `vkm play` sets it.
    std::string fileName = logFileName;
    if (const char* suffix = std::getenv("VKM_LOG_SUFFIX"); suffix && *suffix) {
        const std::filesystem::path named(fileName);
        fileName = named.stem().string() + "-" + suffix + named.extension().string();
    }

    std::filesystem::path logPath = root / "logs" / fileName;
    if (!logFileWritable(logPath)) {
        // Named after the project: one state directory serves every game this
        // engine ships.
        logPath = ProjectPaths::userLogs() / root.filename() / fileName;
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

bool bootGameplayModule(ScriptModule& module, const char* verb) {
    std::error_code ec;

    // Cooked assets only: no Assimp, no image decode. Must precede scene I/O.
    registerCookedAssetFactories();
    AssetLibrary::get().load();

    const std::filesystem::path modulePath =
        ProjectPaths::projectBin() / DynamicLibrary::platformName("game");

    if (!std::filesystem::exists(modulePath, ec)) {
        LOG_ERROR("No gameplay module at '%s' - build the project before %s it",
                  modulePath.string().c_str(), verb);
        return false;
    }
    if (!module.load(modulePath.string())) {
        LOG_ERROR("Gameplay module '%s' did not load", modulePath.string().c_str());
        return false;
    }
    return true;
}

} // namespace Vkm::Engine
