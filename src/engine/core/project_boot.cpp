#define VKM_LOG_CATEGORY "PROJECT"

#include "core/project_boot.h"

#include <filesystem>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "core/engine.h"
#include "debug/error/engine_error_log.h"
#include "ecs/scene.h"
#include "io/project/project.h"
#include "io/asset/asset_library.h"
#include "io/project/roots.h"
#include "io/scene/scene_fingerprint.h"
#include "io/scene/scene_serializer.h"
#include "net/net_session.h"
#include "core/input_map.h"
#include "resource/resource_manager.h"
#include "system/script/behavior_system.h"
#include "system/script/module/script_module.h"

#include "io/scene/default_scene.h"

namespace Vkm::Engine {

SceneBootResult bootProjectWorld(
    const Project& project,
    Scene& scene,
    ResourceManager& resources,
    Clock& clock,
    InputMap& input,
    NetSession& net
) {
    // Before anything in the project ticks or joins; one editor session can open a second project.
    clock.setTickRate(project.tickRate);
    input.setActions(project.actions);

    SceneBootResult boot;
    if (!project.entryScene.empty()) {
        const std::filesystem::path path = Roots::resolveProjectPath(project.entryScene);

        if (SceneSerializer::load(scene, resources, path.string())) {
            LOG_INFO("Opened scene '%s'", path.string().c_str());
            boot = {SceneBoot::Project, path.string()};
        } else {
            reportError("Scene", path.string(), "entry scene failed to load; the default scene stands in");
            buildDefaultScene(scene, resources);
            boot = {SceneBoot::Failed, {}};
        }
    } else {
        buildDefaultScene(scene, resources);
        LOG_INFO("Project '%s' names no entry scene; opened the default scene", project.name.c_str());
    }

    // Zero, compared against nothing, unless an authored file stands behind the world.
    const std::string relative =
        boot.path.empty() ? std::string{} : Roots::toProjectRelative(boot.path);
    net.setWorld(fingerprintScene(boot.path), scene, relative);
    return boot;
}

bool bootPlayedWorld(const Project& project, Engine& engine, BehaviorSystem& behaviors, const char* verb) {
    const SceneBootResult boot = bootProjectWorld(
        project,
        engine.getScene(),
        engine.getResources(),
        engine.getClock(),
        engine.getInput(),
        engine.getNet()
    );
    if (boot.source != SceneBoot::Project) {
        LOG_ERROR("Project '%s' has no world of its own to %s", project.name.c_str(), verb);
        return false;
    }
    behaviors.setPlayedScene(boot.path);
    return true;
}

bool bootGameplayModule(ScriptModule& module, const char* verb) {
    std::error_code ec;

    // Cooked assets only: no recipe import is installed. Must precede scene I/O.
    AssetLibrary::get().load(AssetLibrary::Truth::Manifest);

    const std::filesystem::path modulePath = Roots::gameplayModule();
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
