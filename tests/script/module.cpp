// The project's gameplay module as a host loads it: where it is, what it refuses, what a reload keeps,
// and that no copy of it is left behind. The modules are the fixture's (tests/fixture/), copied into a
// project.

#include <filesystem>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "debug/error/engine_error_log.h"
#include "ecs/component/scene/transform.h"
#include "io/project/roots.h"
#include "io/scene/component_serializer.h"
#include "net/net_session.h"
#include "net/wire/schema.h"
#include "system/script/behavior_registry.h"
#include "system/script/behavior_system.h"
#include "system/script/module/script_module.h"
#include "system/script/script_component.h"
#include "vkm_platform/process/this_process.h"

#include "fixture/fixture.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// How many mapped copies of the module stand beside it in bin/.
int copiesBeside(const std::filesystem::path& module) {
    const std::string prefix = module.stem().string() + ".loaded.";
    int copies = 0;
    std::error_code ec;
    for (const auto& file : std::filesystem::directory_iterator(module.parent_path(), ec)) {
        copies += file.path().filename().string().rfind(prefix, 0) == 0;
    }
    return copies;
}

/// An entity carrying the module's Counter as a scene file would give it: by name, with its fields.
EntityId addCounter(Scene& scene, float speed, const std::string& label) {
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    nlohmann::json script;
    script["behaviors"] = {{{"type", "Counter"}, {"properties", {{"speed", speed}, {"label", label}}}}};
    ScriptComponent component;
    ComponentSerializer::load(script, component);
    scene.add(id, std::move(component));
    return id;
}

/// The Counter's fields as its entity's Script would save them.
nlohmann::json counterOf(Scene& scene, EntityId id) {
    return ComponentSerializer::save(scene.get<ScriptComponent>(id))["behaviors"][0]["properties"];
}

/// Put fixture @p name over the project's module, as a new build lands.
void rebuildAs(const std::string& name) {
    std::error_code ec;
    std::filesystem::copy_file(
        fixtureModule(name),
        Roots::gameplayModule(),
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
    REQUIRE(!ec);
}

} // namespace

TEST("an example runs the module its host's tree built, and only build/ builds one into the example") {
    // Every engine tree builds the examples: were two to write one file, a host would run whichever
    // built last, compiled against another tree's flags.
    const std::filesystem::path example = engineDir() / "examples" / "the_lab";
    Roots::setProjectRoot(example);
    const auto real = [](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::weakly_canonical(path, ec);
    };
    const std::filesystem::path built = real(VKM_TEST_EXAMPLE_MODULE);
    CHECK(real(Roots::gameplayModule()) == built);

    const std::string tree = real(engineDir() / "build").generic_string() + "/";
    const bool primary     = real(Vkm::Platform::executableDir()).generic_string().rfind(tree, 0) == 0;
    CHECK((built.parent_path() == real(example / "bin")) == primary);
}

TEST("the module is loaded from a copy, so a new build can land while it runs, and no copy is left behind") {
    openProject();
    const std::filesystem::path module = installModule("game_v1");
    // What a crash left from an earlier run: the copy of a process no longer running (none runs as 0).
    writeText(
        module.parent_path() / (module.stem().string() + ".loaded.0.7" + module.extension().string()),
        "x"
    );
    {
        ScriptModule loader;
        EventBus events;
        REQUIRE(loader.load(module, nullptr));
        CHECK(loader.isLoaded());
        CHECK(loader.modulePath() == module);
        CHECK(BehaviorRegistry::get().contains("Counter"));
        CHECK(copiesBeside(module) == 1);

        rebuildAs("game_v2");
        Scene scene;
        BehaviorSystem behaviors;
        REQUIRE(loader.reload(scene, behaviors, events));
        CHECK(copiesBeside(module) == 1);

        loader.unload(events);
        CHECK(!loader.isLoaded());
        CHECK(!BehaviorRegistry::get().contains("Counter"));
        CHECK(copiesBeside(module) == 0);

        REQUIRE(loader.load(module, &events));
        CHECK(copiesBeside(module) == 1);
    }
    CHECK(copiesBeside(module) == 0);
    CHECK(std::filesystem::exists(module));
}

TEST("a module built for another engine, for none, or with no entry to register behaviors is refused, saying what to do") {
    struct Refusal {
        const char* fixture;
        std::string says;
    };
    const Refusal refusals[] = {
        {"game_stale", std::string("was built against vkmEngine 0.0.0 but this is ") + VKM_ENGINE_VERSION
                           + ". Rebuild the module (vkm build)."},
        {"game_unversioned", std::string("declares no engine version. Rebuild it against vkmEngine ")
                                 + VKM_ENGINE_VERSION},
        {"game_unregistering", "has no vkmRegisterBehaviors entry"},
    };
    for (const Refusal& refusal : refusals) {
        // A project each: a library mapped once may stay mapped under its name (Tracy keeps every one).
        openProject(refusal.fixture);
        const std::filesystem::path module = installModule(refusal.fixture);
        EngineErrorLog errors;
        setErrorSink(&errors);
        ScriptModule loader;
        CHECK(!loader.load(module, nullptr));
        setErrorSink(nullptr);
        CHECK(!loader.isLoaded());
        CHECK(countInLog(refusal.says) == 1);
        CHECK(copiesBeside(module) == 0);
        REQUIRE(errors.entries().size() == 1);
        CHECK(errors.entries()[0].category == "Script");
    }

    // A file that is no library, and none at all.
    openProject("no_library");
    writeText(Roots::gameplayModule(), "not a library");
    ScriptModule loader;
    CHECK(!loader.load(Roots::gameplayModule(), nullptr));
    CHECK(countInLog("dlopen failed") + countInLog("LoadLibrary") == 1);
    std::filesystem::remove(Roots::gameplayModule());
    CHECK(!loader.load(Roots::gameplayModule(), nullptr));
    CHECK(countInLog("Game module not found at") == 1);
}

TEST("a new build reloads into the running game: each behavior keeps its fields and starts again on the new code") {
    openProject();
    installModule("game_v1");
    ScriptModule loader;
    REQUIRE(loader.load(Roots::gameplayModule(), nullptr));
    Game game;
    const EntityId crate = addCounter(game.scene(), 2.0f, "kept");

    nlohmann::json before;
    bool reloaded = false;
    game.run(120, [&](int frame) {
        if (frame != 60) return;
        before = counterOf(game.scene(), crate);
        rebuildAs("game_v2");
        reloaded = loader.reload(game.scene(), game.systems.behaviors, game.events());
    });

    REQUIRE(reloaded);
    const nlohmann::json after = counterOf(game.scene(), crate);
    CHECK(before["build"] == 1);
    CHECK(before["starts"] == 1);
    CHECK(after["build"] == 2);
    CHECK(after["starts"] == 2);
    CHECK(after["speed"] == 2.0f);
    CHECK(after["label"] == "kept");
    // It heard the new build's beats on the bus the old build's went with.
    CHECK(after["beats"].get<int>() > before["beats"].get<int>());
    // Two seconds at 2 m/s, the reload costing no tick.
    CHECK_NEAR(game.scene().get<Transform>(crate).position.x, 4.0f, 1e-4f);
}

TEST("a reload that finds a broken build keeps the module running, with its behaviors, and says why") {
    openProject();
    installModule("game_v1");
    ScriptModule loader;
    REQUIRE(loader.load(Roots::gameplayModule(), nullptr));
    Game game;
    const EntityId crate = addCounter(game.scene(), 1.0f, "steady");

    EngineErrorLog errors;
    bool reloaded = true;
    game.run(60, [&](int frame) {
        if (frame != 30) return;
        rebuildAs("game_stale");
        setErrorSink(&errors);
        reloaded = loader.reload(game.scene(), game.systems.behaviors, game.events());
        setErrorSink(nullptr);
    });

    CHECK(!reloaded);
    CHECK(loader.isLoaded());
    CHECK(BehaviorRegistry::get().contains("Counter"));
    const nlohmann::json counter = counterOf(game.scene(), crate);
    CHECK(counter["starts"] == 1);
    CHECK(counter["build"] == 1);
    CHECK_NEAR(game.scene().get<Transform>(crate).position.x, 1.0f, 1e-4f);
    REQUIRE(errors.entries().size() == 1);
    CHECK(errors.entries()[0].message.find("Reload refused") == 0);
}

TEST("behaviors held as text while no module is loaded come to life, fields and all, when one is built") {
    openProject();
    ScriptModule loader;
    Game game;
    loader.expect(Roots::gameplayModule(), game.events());
    const EntityId crate = addCounter(game.scene(), 3.0f, "waiting");
    CHECK(game.scene().get<ScriptComponent>(crate).unknown.size() == 1);

    bool loaded = false;
    game.run(70, [&](int frame) {
        if (frame != 10) return;
        installModule("game_v1");
        loaded = loader.reload(game.scene(), game.systems.behaviors, game.events());
    });

    REQUIRE(loaded);
    const ScriptComponent& script = game.scene().get<ScriptComponent>(crate);
    CHECK(script.unknown.empty());
    REQUIRE(script.behaviors.size() == 1);
    const nlohmann::json counter = counterOf(game.scene(), crate);
    CHECK(counter["speed"] == 3.0f);
    CHECK(counter["label"] == "waiting");
    CHECK(counter["starts"] == 1);
    // Sixty ticks at 3 m/s, from the tick after the load.
    CHECK_NEAR(game.scene().get<Transform>(crate).position.x, 3.0f, 1e-4f);
}

TEST("a module's network entry adds its types after the engine's, and a module without one says it has none") {
    openProject();
    installModule("game_v1");
    {
        ScriptModule loader;
        REQUIRE(loader.load(Roots::gameplayModule(), nullptr));
        NetSession session;
        CHECK(loader.setupNetwork(session));
        CHECK(NetSchema::get().indexOf("Transform") == 0);
        CHECK(NetSchema::get().indexOf("Health") == static_cast<int>(NetSchema::get().size()) - 1);
    }
    CHECK(NetSchema::get().size() == 0u);

    openProject("offline");
    installModule("game_offline");
    ScriptModule loader;
    REQUIRE(loader.load(Roots::gameplayModule(), nullptr));
    NetSession session;
    CHECK(!loader.setupNetwork(session));
}
