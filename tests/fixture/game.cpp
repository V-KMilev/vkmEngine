// The gameplay module the tests load, compiled as a project's is (vkm_gameplay_module_options) and
// built twice, VKM_FIXTURE_BUILD 1 and 2, so a reload can be seen running the new build's code.

#include <chrono>
#include <cstdlib>
#include <thread>

#include "system/script/behavior_api.h"

#include "net/wire/schema.h"

namespace Fixture {

using namespace Vkm::Engine;

/// Sent by a Counter on every tick and heard by it: an event of the module's own, whose bus is module
/// code.
struct Beat {
    int count = 1;
};

/// Moves its entity along X at its speed where this end simulates it, and counts its starts and the
/// beats it heard.
class Counter : public Behavior<Counter> {
    public:
        void onStart() override {
            ++starts;
            build = VKM_FIXTURE_BUILD;
            subscribe([this](const Beat& beat) { beats += beat.count; });
        }

        void onFixedUpdate(float dt) override {
            events().enqueue(Beat{});
            if (!isSimulated()) return;
            if (Transform* at = tryGet<Transform>()) at->position.x += speed * dt;
        }

    public:
        float       speed  = 1.0f;
        std::string label;
        int         starts = 0;
        int         build  = 0;  ///< Which build last started it.
        int         beats  = 0;
};

/**
 * @brief What a player carries besides the engine's components, so the schema holds a type of the game's.
 */
struct Health {
    float points = 100.0f;
};

} // namespace Fixture

namespace {

// As slow to load as a large game's when VKM_FIXTURE_LOAD_SECONDS says so, for a test that stops a
// host while it boots.
[[maybe_unused]] const bool g_loaded = [] {
    if (const char* seconds = std::getenv("VKM_FIXTURE_LOAD_SECONDS")) {
        std::this_thread::sleep_for(std::chrono::duration<double>(std::atof(seconds)));
    }
    return true;
}();

} // namespace

VKM_BEHAVIOR(::Fixture::Counter)
    VKM_F(speed)
    VKM_F(label)
    VKM_F(starts)
    VKM_F(build)
    VKM_F(beats)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Fixture::Health)
    VKM_F(points)
VKM_REFLECT_END()

VKM_GAME_MODULE()

// A player is an entity 3 m along X per player number, with its Health.
VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    using namespace Vkm::Engine;

    NetSchema::get().replicate<Fixture::Health>("Health");
    session.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId id) {
            const EntityId player = scene.createEntity();
            Transform at;
            at.position = {3.0f * static_cast<float>(id), 0.0f, 0.0f};
            scene.add(player, at);
            scene.add(player, Fixture::Health{});
            return player;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId player) { scene.destroyEntity(player); }
    );
}
