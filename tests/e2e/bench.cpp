// The bench `vkm_runtime --bench` runs, here in this process on a game with no display, which the host
// would need to draw on: a run passes only once it has finished and written its report.

#include <filesystem>
#include <string>
#include <utility>

#include "app/runtime/bench_system.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using Vkm::App::BenchPlan;
using Vkm::App::BenchSystem;

namespace {

/// A plan of one view at one tier, @p seconds of frames after no warm-up, under a budget no frame misses.
BenchPlan planOf(const std::filesystem::path& root, float seconds) {
    const std::filesystem::path file = root / "bench.json";
    writeText(
        file,
        R"({ "warmupSeconds": 0, "seconds": )" + std::to_string(seconds)
            + R"(, "tiers": [{ "tier": "Low", "averageMs": 100000, "low1Ms": 100000 }],)"
            + R"( "views": [{ "name": "spawn" }] })"
    );
    BenchPlan plan;
    REQUIRE(Vkm::App::loadBenchPlan(file, plan));
    return plan;
}

/**
 * @brief Run @p plan on a game, as vkm_runtime adds the bench.
 *
 * @param frames How many frames at most, a tick each: the bench closes the window once it finishes, and
 *               the engine's ident holds it back for IDENT_SECONDS, the first 600.
 * @return Whether the bench passed.
 */
bool benchPasses(BenchPlan plan, int frames) {
    Game game;
    const BenchSystem& bench = game.engine.addSystem<BenchSystem>(SystemStage::Simulation, std::move(plan));
    game.run(frames);
    return bench.passed();
}

} // namespace

TEST("a bench closed before its last view fails, and says it wrote no report") {
    const std::filesystem::path root = openProject("bench");

    CHECK(!benchPasses(planOf(root, 600.0f), 3));
    CHECK(countInLog("The bench was closed before its last view: it wrote no report, and it fails") == 1);
    CHECK(!std::filesystem::exists(root / "logs" / "bench" / "report.json"));
}

TEST("a finished bench passes with its report written, and fails when the report cannot be") {
    const std::filesystem::path written = openProject("written");
    CHECK(benchPasses(planOf(written, 0.0f), 1000));
    CHECK(std::filesystem::exists(written / "logs" / "bench" / "report.json"));

    // A file where the report's folder would go.
    const std::filesystem::path refused = openProject("refused");
    writeText(refused / "logs" / "bench", "not a folder");
    CHECK(!benchPasses(planOf(refused, 0.0f), 1000));
    CHECK(countInLog("Bench report '") == 1);
    CHECK(countInLog("closed before its last view") == 0);
}
