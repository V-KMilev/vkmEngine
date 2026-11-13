// A project made, cooked by vkm_cook and played as the runtime plays it, and the cook's own promises:
// no display, and nothing baked twice.

#include <filesystem>
#include <map>
#include <string>
#include <system_error>

#include "core/project_boot.h"
#include "io/project/project.h"
#include "io/scene/scene_serializer.h"
#include "system/script/module/script_module.h"

#include "e2e/game_project.h"
#include "e2e/hosts.h"
#include "fixture/fixture.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// Each cooked file of the open project, and when it was written.
std::map<std::string, std::filesystem::file_time_type> cookedFiles() {
    std::map<std::string, std::filesystem::file_time_type> files;
    std::error_code ec;
    for (const auto& file : std::filesystem::recursive_directory_iterator(Roots::cooked(), ec)) {
        if (file.path().extension() == ".vkmc") files[file.path().string()] = file.last_write_time();
    }
    return files;
}

std::string cookedFileOf(const std::string& mesh) {
    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    const AssetRecord* record = AssetLibrary::get().find(AssetType::Mesh, mesh);
    return record ? AssetLibrary::cookedPath(
        AssetType::Mesh,
        mesh,
        record->recipeHash
    ).string() : std::string();
}

} // namespace

TEST("a project cooked by vkm_cook plays as its behaviors say, and what it saves loads back the same") {
    const std::filesystem::path root = openProject("game");
    writeProjectFile("scenes/main.json");
    writeMeshRecipe("game:crate", "cube");
    authorScene("scenes/main.json", {{"Crate", "game:crate", {0.0f, 0.5f, 0.0f}, 2.0f}});
    installModule("game_v1");

    const Ran cook = runHost("vkm_cook", {root.string()});
    REQUIRE(cook.ended);
    CHECK(cook.exit == 0);

    // As vkm_runtime plays it: cooked assets only, no importer, the project's module and world.
    ScriptModule module;
    REQUIRE(bootGameplayModule(module, "playing"));
    Project project;
    loadProject(root, project);
    Game game;
    const SceneBootResult boot = bootProjectWorld(
        project,
        game.scene(),
        game.resources(),
        game.clock(),
        game.engine.getInput(),
        game.engine.getNet()
    );
    REQUIRE(boot.source == SceneBoot::Project);
    const EntityId crate = entityNamed(game.scene(), "Crate");
    REQUIRE(crate);

    // A world arriving holds the clock, so the ticks run are counted rather than assumed.
    game.run(10000, [&](int) {
        if (game.clock().getTick() >= 60) game.engine.getWindow().requestClose();
    });
    const uint32_t ticks = game.clock().getTick();
    CHECK(ticks >= 60u);

    const MeshAsset* mesh = game.resources().tryGet(game.scene().get<Mesh>(crate).mesh);
    REQUIRE(mesh);
    CHECK(mesh->indices.size() == 36u);
    CHECK_NEAR(
        game.scene().get<Transform>(crate).position.x,
        2.0f * static_cast<float>(ticks) / 60.0f,
        1e-4f
    );
    const nlohmann::json counter =
        ComponentSerializer::save(game.scene().get<ScriptComponent>(crate))["behaviors"][0]["properties"];
    CHECK(counter["starts"] == 1);
    CHECK(counter["label"] == "Crate");

    const std::string saved = (root / "scenes" / "saved.json").string();
    REQUIRE(SceneSerializer::save(game.scene(), game.resources(), saved));
    Frame back;
    REQUIRE(SceneSerializer::load(back.scene, back.resources, saved));
    CHECK(SceneSerializer::saveToString(back.scene, back.resources)
          == SceneSerializer::saveToString(game.scene(), game.resources()));
}

TEST("vkm_cook needs no display, bakes nothing twice, and re-bakes a changed recipe's asset alone") {
    const std::filesystem::path root = openProject("game");
    writeProjectFile("scenes/main.json");
    writeMeshRecipe("game:crate", "cube");
    writeMeshRecipe("game:ball", "sphere", 8);
    authorScene("scenes/main.json", {{"Crate", "game:crate", {0.0f, 0.5f, 0.0f}},
                                     {"Ball", "game:ball", {2.0f, 0.5f, 0.0f}}});

    const Ran first = runHost("vkm_cook", {root.string()});
    REQUIRE(first.ended);
    CHECK(first.exit == 0);
    const auto baked = cookedFiles();
    CHECK(baked.size() == 2u);
    const std::string crate = cookedFileOf("game:crate");
    const std::string ball  = cookedFileOf("game:ball");
    CHECK(baked.count(crate) == 1u);
    CHECK(baked.count(ball) == 1u);

    const Ran second = runHost("vkm_cook", {root.string()});
    CHECK(second.exit == 0);
    CHECK(cookedFiles() == baked);

    writeMeshRecipe("game:ball", "sphere", 12);
    const Ran third = runHost("vkm_cook", {root.string()});
    CHECK(third.exit == 0);
    const auto rebaked = cookedFiles();
    const std::string newBall = cookedFileOf("game:ball");
    CHECK(rebaked.size() == 2u);
    CHECK(newBall != ball);
    CHECK(rebaked.count(newBall) == 1u);
    REQUIRE(rebaked.count(crate) == 1u);
    CHECK(rebaked.at(crate) == baked.at(crate));
}
