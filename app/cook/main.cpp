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
#include "cook/recipe_registration.h"
#include "cook/asset_cooker.h"
#include "debug/engine_error_log.h"
#include "io/asset/asset_library.h"
#include "platform/threading/thread_pool.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "project_boot.h"

namespace {

// Cooks a project's assets without opening a window: importing source art,
// writing the binaries and updating the manifest are all CPU work, so an
// unattended build gets a shippable project without opening the editor and
// saving one. The project is the one bootHost resolves.
int cook(int argc, char** argv) {
    try {
        // First: everything below logs to the file this opens and resolves paths
        // against the project it finds. Its own log file and tag, because a cook
        // is a separate run from the editor session that may have launched it.
        if (!Vkm::Engine::bootHost(argc, argv, "cook.log", "VKM-COOK")) return EXIT_FAILURE;
        if (!Vkm::Engine::refuseExtraArgs(argc, argv, "vkm_cook")) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();

        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        // Every scene, not only the entry one: an asset reachable from a second
        // level must be baked too, or it is missing on the one machine with no
        // source art to fall back on.
        std::vector<std::filesystem::path> scenePaths;
        std::error_code ec;
        // Stepped by hand: the range-for's error_code covers the constructor
        // alone, and a step that fails throws.
        namespace fs = std::filesystem;
        const fs::path scenesDir = Vkm::Engine::ProjectPaths::scenes();
        for (fs::directory_iterator it(scenesDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".json") scenePaths.push_back(it->path());
        }
        // A walk that stopped early leaves a short list that looks exactly like
        // a complete one, so nothing is cooked from a sweep that failed. scenes/
        // not being there is the benign case; the empty-list branch below has it.
        if (ec && ec != std::errc::no_such_file_or_directory) {
            LOG_ERROR(
                "Could not read '%s': %s. A partial sweep would cook a "
                "partial game, so nothing was cooked.",
                scenesDir.string().c_str(),
                ec.message().c_str()
            );
            return EXIT_FAILURE;
        }
        // Directory order is whatever the filesystem says, and a cook that
        // visits its scenes in a different order on two machines is a cook
        // whose output cannot be compared.
        std::sort(scenePaths.begin(), scenePaths.end());

        // The entry scene need not live in scenes/, and if it does it is
        // already above - so add it only when the sweep did not find it.
        if (!project.entryScene.empty()) {
            const std::filesystem::path entry = root / project.entryScene;
            const auto isEntry = [&](const std::filesystem::path& p) {
                std::error_code cmpEc;
                return std::filesystem::equivalent(p, entry, cmpEc);
            };
            const bool alreadyFound = std::any_of(scenePaths.begin(), scenePaths.end(), isEntry);
            if (!alreadyFound) scenePaths.insert(scenePaths.begin(), entry);
        }

        // Before any scene I/O; see registerRecipeAssetFactories.
        Vkm::Engine::registerRecipeAssetFactories();
        Vkm::Engine::AssetLibrary::get().load(Vkm::Engine::AssetLibrary::Truth::Recipes);

        // What the library already holds first, scenes or not: a scene load is
        // served from the cache and would never see that the art behind a cached
        // file was re-exported.
        if (!Vkm::Engine::AssetCooker::cookStaleAssets()) {
            LOG_ERROR("Re-baking the assets that changed since their last cook did not complete");
            return EXIT_FAILURE;
        }

        // One at a time: SceneSerializer::load swaps the resource graph in
        // rather than adding to it, so the artifacts accumulate on disk instead.
        for (const std::filesystem::path& scenePath : scenePaths) {
            Vkm::Engine::Scene scene;
            Vkm::Engine::ResourceManager resources;

            // A load that resolved nothing still returns true, so the cook counts
            // failures off the error seam instead; see docs/reference/io.md,
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
            // assets, and each is already named above, so this counts them rather
            // than naming them again.
            if (const unsigned long long failures = loadErrors.totalPushed(); failures > 0) {
                LOG_ERROR(
                    "'%s' loaded with %llu failure(s), each reported above, so a cook of "
                    "it would ship a world with the slots they left empty. An asset the "
                    "library does not hold is baked in by opening the project in the "
                    "editor and saving it; a file the scene names and the project does "
                    "not have has to be put back.",
                    scenePath.string().c_str(),
                    failures
                );
                return EXIT_FAILURE;
            }

            // The sweep left every cooked file current; this records what only a loaded scene can -
            // materials, whose recipes need their textures resolved. Its result is the exit code.
            if (!Vkm::Engine::AssetCooker::cookAllAssets(resources)) {
                LOG_ERROR("Cooking '%s' did not complete", scenePath.string().c_str());
                return EXIT_FAILURE;
            }
            LOG_INFO("Cooked '%s'", scenePath.string().c_str());
        }

        if (scenePaths.empty()) {
            // Nothing to cook from: a project whose world is generated builds its
            // assets at run time, so there is no source art to bake. Not an error.
            LOG_INFO("Project '%s' has no scenes; nothing to cook", project.name.c_str());
        } else {
            LOG_INFO("Cooked %zu scene(s) of '%s'", scenePaths.size(), project.name.c_str());
        }

        // Last, once every record this cook moves has moved: what is left in
        // cooked/ that no record names is nobody's, and would ship in a package.
        Vkm::Engine::AssetLibrary::get().removeUnrecordedCooked();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    const int status = cook(argc, argv);
    // See ThreadPool::shutdown.
    Vkm::Engine::ThreadPool::get().shutdown();
    return status;
}
