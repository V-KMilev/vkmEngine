#define VKM_LOG_CATEGORY "PROJECT"

#include "project_boot.h"

#include <filesystem>

#include "logger.h"

#include "ecs/scene.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"
#include "system/script/script_module.h"

#include "generator/default_scene.h"

namespace Engine {

SceneSource bootProjectScene(const Project& project, ScriptModule& module,
                             Scene& scene, ResourceManager& resources) {
    if (!project.entryScene.empty()) {
        const std::filesystem::path path =
            (ProjectPaths::projectRoot() / project.entryScene).lexically_normal();

        if (SceneSerializer::load(scene, resources, path.string())) {
            LOG_INFO("Opened scene '%s'", path.string().c_str());
            return SceneSource::Authored;
        }
        // Fall through rather than leave an empty world: the project named a
        // scene, so the miss is worth an error even though it is recoverable.
        LOG_ERROR("Entry scene '%s' failed to load; opening the default scene",
                  path.string().c_str());
    } else if (module.buildScene(scene)) {
        LOG_INFO("Scene built by the project's module");
        return SceneSource::Generated;
    }

    buildDefaultScene(scene, resources);
    LOG_INFO("Project '%s' supplies no scene of its own; opened the default scene",
             project.name.c_str());
    return SceneSource::Default;
}

} // namespace Engine
