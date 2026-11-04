// What a behavior reaches from behavior_api.h, the one header a project's module includes: the harness
// included after it declares none of what the behaviors here call.

#include "system/script/behavior_api.h"

#include "harness/check.h"
#include "harness/world.h"

namespace ScriptApi {

using namespace Vkm::Engine;

/**
 * @brief Counts its starts across runs in its player's own file, as the Save progress recipe keeps a score.
 */
class RunCounter : public Behavior<RunCounter> {
    public:
        void onStart() override {
            const auto file     = Roots::playerFile("progress.json");
            nlohmann::json save = readOwnFile(file);
            save["runs"] = save.value("runs", 0) + 1;
            writeOwnFile(file, save);
        }
};

} // namespace ScriptApi

VKM_BEHAVIOR(::ScriptApi::RunCounter)
VKM_REFLECT_END()

using namespace Vkm::Test;

TEST("a behavior keeps its player's progress with readOwnFile and writeOwnFile, from behavior_api.h alone") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Tower Climb"})");
    for (int run = 0; run < 2; ++run) {
        Game game;
        addBehavior<ScriptApi::RunCounter>(game.scene(), game.scene().createEntity());
        game.run(1);
    }
    CHECK(readOwnFile(Roots::playerFile("progress.json"))["runs"] == 2);
}
