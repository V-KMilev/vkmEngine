// Opening a project's world as every host but the cooker does: what was saved is what plays; and each
// host on a project keeps a log of its own.

#include <string>

#include <nlohmann/json.hpp>

#include "core/project_boot.h"
#include "debug/error/engine_error_log.h"
#include "io/project/host_boot.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace {

SceneBootResult boot(Game& game, const std::filesystem::path& root) {
    Project project;
    loadProject(root, project);
    return bootProjectWorld(
        project,
        game.scene(),
        game.resources(),
        game.clock(),
        game.engine.getInput(),
        game.engine.getNet()
    );
}

} // namespace

TEST("a scene saved as the editor saves it is the world the game opens, on the project's clock and actions") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Opened", "tickRate": 30, "actions": {"Jump": ["Space"]}})");

    // Authored: a few things and the scene's own settings, saved with Save As's first save.
    Scene authored;
    ResourceManager resources;
    const EntityId sun = authored.createEntity();
    authored.add(sun, Transform{});
    Light light;
    light.intensity = 4.0f;
    authored.add(sun, light);
    const EntityId eye = authored.createEntity();
    Transform back;
    back.position = {0.0f, 2.0f, 8.0f};
    authored.add(eye, back);
    authored.add(eye, Camera{});
    authored.environment().fog.density *= 3.0f;
    authored.physics().gravity = {0.0f, -4.0f, 0.0f};
    REQUIRE(SceneSerializer::save(authored, resources, (root / "scenes" / "level.json").string()));
    REQUIRE(setEntrySceneIfNone(root, "scenes/level.json"));
    CHECK(!setEntrySceneIfNone(root, "scenes/other.json"));

    Game game;
    const SceneBootResult opened = boot(game, root);
    CHECK(opened.source == SceneBoot::Project);
    CHECK(std::filesystem::path(opened.path) == root / "scenes" / "level.json");
    CHECK(SceneSerializer::saveToString(
        game.scene(),
        game.resources()
    ) == SceneSerializer::saveToString(authored, resources));
    CHECK(game.clock().getFixedStep() == 1.0f / 30.0f);
    CHECK(game.engine.getInput().bindings("Jump").size() == 1u);

    game.run(3);
    CHECK(game.clock().getTick() == 3u);
}

TEST("an entry scene that will not load leaves the default scene and says so, and none named opens it quietly") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Broken", "entryScene": "scenes/gone.json"})");
    EngineErrorLog errors;
    setErrorSink(&errors);
    Game failed;
    const SceneBootResult refused = boot(failed, root);
    setErrorSink(nullptr);
    CHECK(refused.source == SceneBoot::Failed);
    // No path: a save asks for a name rather than writing over the file that failed.
    CHECK(refused.path.empty());
    CHECK(failed.scene().entityCount() > 0u);
    REQUIRE(errors.entries().size() == 1u);
    CHECK(errors.entries()[0].source.find("gone.json") != std::string::npos);

    writeText(root / "project.json", R"({"name": "Empty"})");
    Game empty;
    const SceneBootResult standIn = boot(empty, root);
    CHECK(standIn.source == SceneBoot::Default);
    CHECK(standIn.path.empty());
    CHECK(empty.scene().entityCount() == failed.scene().entityCount());
}

TEST("a second host on a project logs to a file of its own, and the first's name is free once that host ends") {
    const std::filesystem::path logs = openProject() / "logs";
    std::filesystem::create_directories(logs);
    LogClaim first = claimLog(logs, "log.log");
    const LogClaim second = claimLog(logs, "log.log");
    CHECK(first.name == "log.log");
    CHECK(first.lock.has_value());
    CHECK(second.name == "log-2.log");
    CHECK(second.lock.has_value());

    first.lock.reset();
    const LogClaim again = claimLog(logs, "log.log");
    CHECK(again.name == "log.log");
}
