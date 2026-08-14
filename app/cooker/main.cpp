#define VKM_LOG_CATEGORY "COOK"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "asset_registration.h"
#include "cook/asset_cooker.h"
#include "io/asset/asset_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"

// Cooks a project's assets without opening a window.
//
// Producing a shippable build used to require opening the editor and saving,
// because that was the only thing that ran the cooker - a GUI step, on a machine
// with a GPU, in the middle of what should be an unattended build. Nothing about
// cooking needs either: it imports source art, writes binaries, and updates the
// manifest, all on the CPU.
//
// Follows the same rule as the other two binaries: the project is the one beside
// this executable, unless an argument names a different one.
int main(int argc, char** argv) {
    try {
        std::error_code ec;
        if (argc > 1) {
            const std::filesystem::path found =
                Engine::findProjectRoot(std::filesystem::absolute(argv[1], ec));
            if (!found.empty()) Engine::ProjectPaths::setProjectRoot(found);
        }

        const std::filesystem::path root = Engine::ProjectPaths::projectRoot();
        std::filesystem::create_directories(root / "logs", ec);

        if (!Logger::init((root / "logs" / "cook.log").string(), "VKM-COOK", LogLevel::TRACE)) {
            return EXIT_FAILURE;
        }

        Engine::Project project;
        Engine::loadProject(root, project);

        if (project.entryScene.empty()) {
            // Nothing to cook from: a project whose world is generated builds its
            // assets at run time, so there is no source art to bake. Not an error.
            LOG_INFO("Project '%s' has no entry scene; nothing to cook", project.name.c_str());
            return EXIT_SUCCESS;
        }

        // Both factory sets: the cooked ones to read what is already baked, the
        // recipe ones to import source art for what is not.
        Engine::registerCookedAssetFactories();
        Engine::registerRecipeAssetFactories();
        Engine::AssetLibrary::get().load();

        // Loading the scene is what pulls its assets in; cooking then bakes
        // exactly what the scene references, which is what the editor does on
        // save and what a shipped build actually needs.
        Engine::Scene scene;
        Engine::ResourceManager resources;

        const std::filesystem::path scenePath = root / project.entryScene;
        if (!Engine::SceneSerializer::load(scene, resources, scenePath.string())) {
            LOG_ERROR("Failed to load '%s'", scenePath.string().c_str());
            return EXIT_FAILURE;
        }

        Engine::AssetCooker::cookAllAssets(resources);
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
