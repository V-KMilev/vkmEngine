#define VKM_LOG_CATEGORY "COOK"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "logger.h"

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "asset_registration.h"
#include "cook/asset_cooker.h"
#include "debug/engine_error_log.h"
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

        // A load that resolved nothing still returns true - the scene is a
        // document, and every unresolved reference is a component slot left
        // empty rather than a parse failure. That is right for the editor,
        // which is where they get fixed, and wrong here: with no reference
        // resolved there is no loaded asset left for the cook below to fail on,
        // so an empty world would sail past the exit code and be packaged. The
        // loader already reports each one through the error seam; the cooker
        // counts them by listening to it rather than by asking the loader to
        // answer a different question for a different host.
        Vkm::Engine::EngineErrorLog loadErrors;
        Vkm::Engine::setErrorSink(&loadErrors);

        const std::filesystem::path scenePath = root / project.entryScene;
        const bool loaded =
            Vkm::Engine::SceneSerializer::load(scene, resources, scenePath.string());

        Vkm::Engine::setErrorSink(nullptr);

        if (!loaded) {
            LOG_ERROR("Failed to load '%s'", scenePath.string().c_str());
            return EXIT_FAILURE;
        }
        // What the sink holds is every failure the load reported, not only the
        // unresolved assets it was installed for: a prefab file that will not
        // open reaches it the same way. Calling all of them unresolved
        // references answered a deleted file with "save the project", which
        // bakes a library that file was never going to be in. Each one is
        // already named above, so this counts them and names the remedies apart.
        if (const unsigned long long failures = loadErrors.totalPushed(); failures > 0) {
            LOG_ERROR("'%s' loaded with %llu failure(s), each reported above, so a cook of "
                      "it would ship a world with the slots they left empty. An asset the "
                      "library does not hold is baked in by opening the project in the "
                      "editor and saving it; a file the scene names and the project does "
                      "not have has to be put back.",
                      scenePath.string().c_str(), failures);
            return EXIT_FAILURE;
        }

        // The exit code is what an unattended build reads. A cook that could not
        // produce part of the library has to say so here, or the failure surfaces
        // as a game that cannot load its own assets.
        if (!Vkm::Engine::AssetCooker::cookAllAssets(resources)) {
            LOG_ERROR("Cooking '%s' did not complete", project.name.c_str());
            return EXIT_FAILURE;
        }
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
