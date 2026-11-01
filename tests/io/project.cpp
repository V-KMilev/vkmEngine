// project.json: what a project says about its game, read forgivingly and written back without
// losing what the author wrote.

#include <string>

#include <nlohmann/json.hpp>

#include "io/project/project.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace {

json fileOf(const std::filesystem::path& root) { return json::parse(readText(root / "project.json")); }

} // namespace

TEST("a missing or malformed project.json keeps every default, takes its folder's name and says why") {
    const std::filesystem::path root = openProject("courtyard");
    const Project defaults;

    Project missing;
    CHECK(!loadProject(root, missing));
    CHECK(missing.name == "courtyard");
    CHECK(missing.tickRate == defaults.tickRate);
    CHECK(sameProjectRender(missing.render, defaults.render));
    CHECK(logText().empty());

    writeText(root / "project.json", "{ \"name\": \"Yard\", ");
    Project malformed;
    CHECK(!loadProject(root, malformed));
    CHECK(malformed.name == "courtyard");
    CHECK(malformed.maxPlayers == defaults.maxPlayers);
    CHECK(countInLog("malformed JSON") == 1);

    // A key of the wrong type costs that key; one out of range is held to its range.
    writeText(root / "project.json", R"({"name": "", "tickRate": "fast", "maxPlayers": 500, "netPort": 30000,
        "render": {"bloom": "yes", "renderScale": 3.0, "msaaSamples": 3, "exposure": 0.5}})");
    Project mistyped;
    CHECK(loadProject(root, mistyped));
    CHECK(mistyped.name == "courtyard");
    CHECK(mistyped.tickRate == defaults.tickRate);
    CHECK(mistyped.maxPlayers == Config::MAX_PLAYERS_PER_GAME);
    CHECK(mistyped.netPort == 30000);
    CHECK(mistyped.render.bloom == defaults.render.bloom);
    CHECK(mistyped.render.renderScale == RenderSettings::MAX_RENDER_SCALE);
    CHECK(mistyped.render.msaaSamples == defaults.render.msaaSamples);
    CHECK(mistyped.render.exposure == 0.5f);
    CHECK(countInLog("'tickRate' holds a value of the wrong type") == 1);
    CHECK(countInLog("'bloom' holds a value of the wrong type") == 1);
    CHECK(countInLog("seats") == 1);
    CHECK(countInLog("renderScale") == 1);
    CHECK(countInLog("msaaSamples") == 1);

    writeText(root / "project.json", R"({"tickRate": 1})");
    Project slow;
    CHECK(loadProject(root, slow));
    CHECK(slow.tickRate == Config::MIN_TICK_RATE);
}

TEST("a project made with another engine version opens, and says so") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Old Game", "engineVersion": "0.0.1", "tickRate": 30})");
    Project project;
    CHECK(loadProject(root, project));
    CHECK(project.tickRate == 30u);
    CHECK(countInLog("authored against engine 0.0.1") == 1);
    CHECK(!compatibleEngine("0.0.1"));
    CHECK(compatibleEngine(""));

    // Named for this engine, it opens with nothing to say.
    REQUIRE(saveProjectEngine(root));
    const std::string thisEngine = fileOf(root)["engineVersion"].get<std::string>();
    CHECK(thisEngine != "0.0.1");
    CHECK(compatibleEngine(thisEngine));
    Project again;
    CHECK(loadProject(root, again));
    CHECK(again.engineVersion == thisEngine);
    CHECK(countInLog("authored against") == 1);
    CHECK(fileOf(root)["name"] == "Old Game");
}

TEST("fonts, logos and the loading screen read by their keys, an entry that will not read skipped and said") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({
        "fonts": [{"name": "Title", "file": "fonts/title.ttf"}, {"name": "NoFile"}, 5, {"name": 3, "file": "x.ttf"}],
        "splash": [{"image": "logo/studio.png", "seconds": 2.5}, {"seconds": 1.0}, {"image": "a.png", "seconds": "long"}],
        "loading": {"tips": ["Jump with Space", "", "Crouch with C"], "screen": "prefabs/load.json", "water": false}})");
    Project project;
    REQUIRE(loadProject(root, project));
    CHECK(project.fonts == std::vector<FontEntry>{{"Title", "fonts/title.ttf"}});
    CHECK(project.splash == std::vector<SplashEntry>{{"logo/studio.png", 2.5f}});
    CHECK(project.loading.tips == std::vector<std::string>{"Jump with Space", "Crouch with C"});
    CHECK(project.loading.screen == "prefabs/load.json");
    CHECK(!project.loading.water);
    CHECK(countInLog("A fonts entry") == 3);
    CHECK(countInLog("A splash entry") == 1);

    // In the loading block, a tip that is no text keeps the tips as they were; a wrong screen or
    // water keeps its own.
    writeText(root / "project.json", R"({"loading": {"tips": ["one", 2], "screen": 5, "water": "no"}})");
    Project wrong;
    REQUIRE(loadProject(root, wrong));
    CHECK(wrong.loading == LoadingScreen{});
    CHECK(countInLog("'tips' holds a value of the wrong type") == 1);
    CHECK(countInLog("'screen' holds a value of the wrong type") == 1);
    CHECK(countInLog("'water' holds a value of the wrong type") == 1);
}

TEST("a project saved keeps what it did not change as written, and writes a changed list whole") {
    const std::filesystem::path root = openProject();
    const json written = json::parse(R"({
        "name": "Keeper", "authorNote": "kept by hand",
        "fonts": [{"name": "Title", "file": "fonts/title.ttf"}, {"name": "NoFile"}],
        "splash": [{"image": "logo/a.png", "seconds": 2}],
        "loading": {"tips": ["one", ""], "water": true}})");
    writeText(root / "project.json", written.dump(4));
    Project project;
    REQUIRE(loadProject(root, project));

    // Unchanged: every list, the block and the key the engine does not know stay as written.
    REQUIRE(saveProject(root, project));
    json saved = fileOf(root);
    CHECK(saved["fonts"] == written["fonts"]);
    CHECK(saved["splash"] == written["splash"]);
    CHECK(saved["loading"] == written["loading"]);
    CHECK(saved["authorNote"] == "kept by hand");

    // Changed: the list is the project's, by its reflected keys.
    project.fonts.push_back({"Body", "fonts/body.ttf"});
    project.loading = LoadingScreen{};
    REQUIRE(saveProject(root, project));
    saved = fileOf(root);
    CHECK(saved["fonts"] == json::parse(R"([{"name": "Title", "file": "fonts/title.ttf"}, {"name": "Body", "file": "fonts/body.ttf"}])"));
    CHECK(!saved.contains("loading"));
    CHECK(saved["splash"] == written["splash"]);
    Project again;
    REQUIRE(loadProject(root, again));
    CHECK(again.fonts == project.fonts);
    CHECK(again.loading == LoadingScreen{});

    // A file that does not parse is not written over.
    writeText(root / "project.json", "{ broken");
    CHECK(!saveProject(root, project));
    CHECK(readText(root / "project.json") == "{ broken");
}

TEST("a project saved writes only the fields away from their defaults, its render block's too") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Lean", "tickRate": 30, "render": {"bloom": false}})");
    Project project;
    REQUIRE(loadProject(root, project));

    // One setting changed in Render Settings: the block holds it and what was already set, no more.
    project.render.gtao = !RenderSettings{}.gtao;
    REQUIRE(saveProjectRender(root, project.render));
    json saved = fileOf(root);
    const json chosen = {{"bloom", false}, {"gtao", project.render.gtao}};
    CHECK(saved["render"] == chosen);
    CHECK(saved["tickRate"] == 30);

    // Back at its default, a field is no key; a block with none is no block.
    project.tickRate = Project{}.tickRate;
    project.render   = RenderSettings{};
    REQUIRE(saveProject(root, project));
    saved = fileOf(root);
    CHECK(!saved.contains("tickRate"));
    CHECK(!saved.contains("render"));
    CHECK(!saved.contains("maxPlayers"));
    CHECK(saved["name"] == "Lean");
    Project again;
    REQUIRE(loadProject(root, again));
    CHECK(again.tickRate == Project{}.tickRate);
    CHECK(sameProjectRender(again.render, RenderSettings{}));
}
