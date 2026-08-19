#define VKM_LOG_CATEGORY "COOK"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "logger.h"

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "asset_registration.h"
#include "cook/asset_cooker.h"
#include "io/asset/asset_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "project_boot.h"

// Cooks a project's assets without opening a window.
//
// Cooking needs no window and no GPU - it imports source art, writes binaries
// and updates the manifest, all on the CPU - so an unattended build does not
// have to open the editor and save to get a shippable one.
//
// Same rule as the other two binaries: the project is the one beside this
// executable, unless an argument names a different one.
int main(int argc, char** argv) {
    try {
        // Project root, working directory and log file, in the one order that
        // works (see tools/project_boot.h). Its own log file and tag: a cook is
        // a separate run from the editor session that may have launched it.
        if (!Vkm::Engine::bootHost(argc, argv, "cook.log", "VKM-COOK")) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();

        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        if (project.entryScene.empty()) {
            // Nothing to cook from: a project whose world is generated builds its
            // assets at run time, so there is no source art to bake. Not an error.
            LOG_INFO("Project '%s' has no entry scene; nothing to cook", project.name.c_str());
            return EXIT_SUCCESS;
        }

        // The recipe factories import source art for what is not baked yet, and
        // fall through to the cooked path for what is.
        Vkm::Engine::registerRecipeAssetFactories();
        Vkm::Engine::AssetLibrary::get().load();

        // Loading the scene is what pulls its assets in; cooking then bakes
        // exactly what it references, which is what a shipped build needs.
        Vkm::Engine::Scene scene;
        Vkm::Engine::ResourceManager resources;

        const std::filesystem::path scenePath = root / project.entryScene;
        if (!Vkm::Engine::SceneSerializer::load(scene, resources, scenePath.string())) {
            LOG_ERROR("Failed to load '%s'", scenePath.string().c_str());
            return EXIT_FAILURE;
        }

        Vkm::Engine::AssetCooker::cookAllAssets(resources);
        LOG_INFO("Cooked '%s'", project.name.c_str());

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
