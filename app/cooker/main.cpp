#define VKM_LOG_CATEGORY "COOK"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

        // Every scene, not only the entry one: an asset reachable from a second
        // level must be baked too, or it is missing on the one machine with no
        // source art to fall back on - the player's.
        std::vector<std::filesystem::path> scenePaths;
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(Vkm::Engine::ProjectPaths::scenes(), ec)) {
            if (entry.path().extension() == ".json") scenePaths.push_back(entry.path());
        }
        // Directory order is whatever the filesystem says, and a cook that
        // visits its scenes in a different order on two machines is a cook
        // whose output cannot be compared.
        std::sort(scenePaths.begin(), scenePaths.end());

        // The entry scene need not live in scenes/, and if it does it is
        // already above - so add it only when the sweep did not find it.
        if (!project.entryScene.empty()) {
            const std::filesystem::path entry = root / project.entryScene;
            const bool alreadyFound = std::any_of(
                scenePaths.begin(), scenePaths.end(),
                [&](const std::filesystem::path& p) {
                    std::error_code cmpEc;
                    return std::filesystem::equivalent(p, entry, cmpEc);
                });
            if (!alreadyFound) scenePaths.insert(scenePaths.begin(), entry);
        }

        if (scenePaths.empty()) {
            // Nothing to cook from: a project whose world is generated builds its
            // assets at run time, so there is no source art to bake. Not an error.
            LOG_INFO("Project '%s' has no scenes; nothing to cook", project.name.c_str());
            return EXIT_SUCCESS;
        }

        // The recipe factories import source art for what is not baked yet, and
        // fall through to the cooked path for what is.
        Vkm::Engine::registerRecipeAssetFactories();
        Vkm::Engine::AssetLibrary::get().load();

        // One at a time: SceneSerializer::load swaps the resource graph in
        // rather than adding to it, so the artifacts accumulate on disk instead.
        for (const std::filesystem::path& scenePath : scenePaths) {
            Vkm::Engine::Scene scene;
            Vkm::Engine::ResourceManager resources;

            // A load that resolved nothing still returns true, so the cook counts
            // failures off the error seam instead; see docs/reference/system/io.md,
            // "What each host does when a project will not open".
            Vkm::Engine::EngineErrorLog loadErrors;
            Vkm::Engine::setErrorSink(&loadErrors);

            const bool loaded =
                Vkm::Engine::SceneSerializer::load(scene, resources, scenePath.string());

            Vkm::Engine::setErrorSink(nullptr);

            if (!loaded) {
                LOG_ERROR("Failed to load '%s'", scenePath.string().c_str());
                return EXIT_FAILURE;
            }
            // The sink holds every failure the load reported, not only unresolved
            // assets, and each is already named above - so this counts them and
            // names the two remedies apart rather than picking one.
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
                LOG_ERROR("Cooking '%s' did not complete", scenePath.string().c_str());
                return EXIT_FAILURE;
            }
            LOG_INFO("Cooked '%s'", scenePath.string().c_str());
        }

        LOG_INFO("Cooked %zu scene(s) of '%s'", scenePaths.size(), project.name.c_str());

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
