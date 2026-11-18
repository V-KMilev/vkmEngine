// What ships is in the form this build writes: every example's and template's scenes, prefabs and
// project.json load and save back as their files say, and nothing in them is a key this build drops.

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/asset/asset_library.h"
#include "io/json/reflect_json.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_assets.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

/// Every folder that holds a project.json or a content pack's prefabs, sorted.
std::vector<fs::path> shippedRoots() {
    std::vector<fs::path> roots;
    for (const char* parent : {"examples", "templates", "content"}) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(engineDir() / parent, ec)) {
            if (entry.is_directory()) roots.push_back(entry.path());
        }
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

/// The .json files under @p folder, any depth, sorted.
std::vector<fs::path> jsonUnder(const fs::path& folder) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(folder, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// The files git keeps under the engine's tree, committed or not: what ships, without the art a person keeps
/// beside it in an ignored folder (an example's library/ but the lab's). Empty when git cannot be run.
std::set<fs::path> trackedFiles() {
    const std::string LIST = "ls-files --cached --others --exclude-standard";  // as the docs suite reads it
#if defined(_WIN32)
    FILE* git = _popen(("git -C \"" + engineDir().string() + "\" " + LIST).c_str(), "r");
#else
    FILE* git = popen(("git -C '" + engineDir().string() + "' " + LIST).c_str(), "r");
#endif
    std::set<fs::path> files;
    if (!git) return files;
    char line[4096];
    while (std::fgets(line, sizeof(line), git)) {
        std::string path(line);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
        files.insert((engineDir() / path).lexically_normal());
    }
#if defined(_WIN32)
    _pclose(git);
#else
    pclose(git);
#endif
    return files;
}

json withoutAssets(json doc) {
    // With nothing cooked here the assets block holds only the names the fields give.
    doc.erase("assets");
    return doc;
}

} // namespace

TEST("every shipped scene and prefab, loaded and saved again, says what its file says") {
    int scenes = 0, prefabs = 0;
    std::vector<std::string> differing;
    for (const fs::path& root : shippedRoots()) {
        Roots::setProjectRoot(root);
        for (const fs::path& file : jsonUnder(root / "scenes")) {
            ++scenes;
            Scene scene;
            ResourceManager resources;
            const fs::path echoed = scratch() / "echo.json";
            const bool echoes     = SceneSerializer::load(scene, resources, file.string())
                && SceneSerializer::save(scene, resources, echoed.string());
            if (!echoes || withoutAssets(
                json::parse(readText(echoed))
            ) != withoutAssets(json::parse(readText(file)))) {
                differing.push_back(file.lexically_relative(engineDir()).generic_string());
            }
        }
        for (const fs::path& file : jsonUnder(root / "prefabs")) {
            ++prefabs;
            Scene scene;
            ResourceManager resources;
            const std::string relative = file.lexically_relative(root).generic_string();
            const EntityId built = Prefab::instantiate(scene, resources, relative);
            const fs::path echoed = scratch() / "echo_prefab.json";
            const bool echoes = built && Prefab::save(scene, built, echoed.string(), resources);
            json said    = withoutAssets(json::parse(readText(file)));
            json written = echoes ? withoutAssets(json::parse(readText(echoed))) : json();
            // The next uid is seeded from the file written over, which here is a new one.
            said.erase("nextUid");
            written.erase("nextUid");
            if (!echoes || written != said) differing.push_back(
                file.lexically_relative(engineDir()).generic_string()
            );
        }
    }
    std::printf("%d scenes, %d prefabs\n", scenes, prefabs);
    for (const std::string& one : differing) std::printf("  differs: %s\n", one.c_str());
    CHECK(scenes >= 5);
    CHECK(prefabs >= 20);
    CHECK(differing.empty());
    CHECK(countInLog("schema drift") == 0);
}

TEST("every shipped library recipe sits where its name says, and a material's holds no key it drops") {
    const std::set<fs::path> tracked = trackedFiles();
    if (tracked.empty()) skip("git cannot list the tree, so what ships is not known");
    int recipes = 0, materials = 0;
    for (const fs::path& root : shippedRoots()) {
        if (!fs::exists(root / "library")) continue;
        Roots::setProjectRoot(root);
        for (const fs::path& file : jsonUnder(root / "library")) {
            if (file.parent_path() == root / "library") continue;  // the library's own index, no recipe
            if (!tracked.count(file.lexically_normal())) continue;  // a person's own art, not shipped
            ++recipes;
            const json doc = json::parse(readText(file));
            const std::string name = doc.value("name", "");
            AssetType found = AssetType::Count;
            for (size_t kind = 0; kind < static_cast<size_t>(AssetType::Count); ++kind) {
                const fs::path named = AssetLibrary::recipePath(static_cast<AssetType>(kind), name);
                if (named.lexically_normal() == file.lexically_normal()) found = static_cast<AssetType>(kind);
            }
            CHECK(found != AssetType::Count);
            if (found == AssetType::Count) {
                std::printf("  %s is not where its name says\n", file.string().c_str());
            }

            // Only a material's recipe is read field by field, warning of a key it drops.
            if (found != AssetType::Material) continue;
            ++materials;
            ResourceManager resources;
            CHECK(SceneAssets::loadNamed(AssetType::Material, name, resources));
        }
    }
    CHECK(recipes > 100);
    CHECK(materials > 20);
    CHECK(countInLog("schema drift") == 0);
}

TEST("every shipped project.json reads, and saves back with its lists as written") {
    int projects = 0;
    for (const fs::path& root : shippedRoots()) {
        if (!fs::exists(root / "project.json")) continue;
        ++projects;
        const json original = json::parse(readText(root / "project.json"));
        Project project;
        REQUIRE(loadProject(root, project));

        // Saved into a copy: the lists, the block and every key it does not own are as written.
        const fs::path copy = scratch() / root.filename();
        fs::create_directories(copy);
        fs::copy_file(root / "project.json", copy / "project.json", fs::copy_options::overwrite_existing);
        REQUIRE(saveProject(copy, project));
        const json saved = json::parse(readText(copy / "project.json"));
        for (const char* list : {"fonts", "splash", "loading", "actions", "physicsLayers"}) {
            CHECK(saved.contains(list) == original.contains(list));
            if (original.contains(list)) CHECK(saved[list] == original[list]);
        }

        // And it reads back as the same project.
        Project again;
        REQUIRE(loadProject(copy, again));
        CHECK(ReflectJson::toJson(again) == ReflectJson::toJson(project));
        CHECK(again.fonts == project.fonts);
        CHECK(again.splash == project.splash);
        CHECK(again.loading == project.loading);
        CHECK(again.physicsLayers == project.physicsLayers);
    }
    CHECK(projects >= 5);
    CHECK(countInLog("skipped") == 0);
}
