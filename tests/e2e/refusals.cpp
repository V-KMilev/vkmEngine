// What a host does with what it cannot use: a flag it does not know, and a project it cannot play. It
// says which, exits 1, and starts nothing.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "e2e/game_project.h"
#include "e2e/hosts.h"
#include "fixture/fixture.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

struct Refusal {
    std::string              host;
    std::vector<std::string> args;
    std::string              says;
};

/// Run each of @p refusals: it must end at once, exit 1, say its line and get no further than that.
void expectRefused(const std::vector<Refusal>& refusals, const std::string& neverSays) {
    for (const Refusal& refusal : refusals) {
        const Ran ran = runHost(refusal.host, refusal.args);
        CHECK(ran.ended);
        CHECK(ran.exit == 1);
        CHECK(countIn(ran.output, refusal.says) >= 1);
        CHECK(countIn(ran.output, neverSays) == 0);
        if (countIn(ran.output, refusal.says) == 0) std::printf(
            "%s %s said:\n%s\n",
            refusal.host.c_str(),
            refusal.args.back().c_str(),
            ran.output.c_str()
        );
    }
}

/// A project every host can open: the fixture's module and a scene of one crate.
std::string playableProject(const std::string& name) {
    const std::filesystem::path root = openProject(name);
    writeProjectFile("scenes/main.json");
    writeMeshRecipe("game:crate", "cube");
    authorScene("scenes/main.json", {{"Crate", "game:crate", {0.0f, 0.5f, 0.0f}, 1.0f}});
    installModule("game_v1");
    return root.string();
}

} // namespace

TEST("a host refuses an argument it does not know, naming it, before it loads anything") {
    const std::string game     = playableProject("game");
    const std::string plan     = (std::filesystem::path(game) / "no_plan.json").string();
    const std::string unbudget = (std::filesystem::path(game) / "unbudgeted.json").string();
    writeText(unbudget, R"({ "tiers": [{ "tier": "Max" }], "views": [{ "name": "spawn" }] })");
    expectRefused({
        {"vkm_runtime", {game, "--bogus"}, "Unrecognized argument '--bogus'"},
        {"vkm_runtime", {game, "--view", "Sideways"}, "--view needs a debug view to show, one of: Lit,"},
        {"vkm_runtime", {game, "--connect"}, "--connect needs an address to join"},
        {"vkm_runtime", {game, "--bench"}, "--bench needs a plan to run"},
        {"vkm_runtime", {game, "--bench", plan}, plan + " is not a bench plan: it is missing or not JSON"},
        {"vkm_runtime", {game, "--bench", unbudget}, unbudget + ": tier 'Max' needs an averageMs and a low1Ms over 0"},
        {"vkm_server", {game, "--port", "70000"}, "'70000' is not a port to serve on"},
        {"vkm_server", {game, "--port"}, "--port needs a number to serve on"},
        {"vkm_server", {game, "--players"}, "Unrecognized argument '--players'"},
        {"vkm_cook", {game, "extra"}, "vkm_cook takes a project directory and nothing else, and 'extra' is neither"},
    }, "Loaded game module");
}

TEST("a host refuses a project it cannot play or cook, saying how to put it right") {
    const std::string unbuilt = playableProject("unbuilt");
    std::filesystem::remove(Roots::gameplayModule());

    const std::string stale = playableProject("stale");
    installModule("game_stale");

    const std::string offline = playableProject("offline");
    installModule("game_offline");

    const std::string worldless = playableProject("worldless");
    writeProjectFile("");

    // A scene naming a mesh the library has no recipe for.
    const std::string missing = playableProject("missing");
    authorScene("scenes/main.json", {{"Rock", "game:rock", {0.0f, 0.0f, 0.0f}}});

    expectRefused({
        {"vkm_runtime", {unbuilt}, "build the project before playing it"},
        {"vkm_server", {unbuilt}, "build the project before serving it"},
        {"vkm_runtime", {stale}, "was built against vkmEngine 0.0.0 but this is " VKM_ENGINE_VERSION
                                 ". Rebuild the module (vkm build)."},
        {"vkm_server", {offline}, "has no vkmSetupNetwork entry, so it cannot take players"},
        {"vkm_server", {worldless}, "has no world of its own to serve"},
        {"vkm_cook", {missing}, "loaded with 1 failure(s)"},
    }, "Serving '");
}
