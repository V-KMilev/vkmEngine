// Plays a project as vkm_server does, with no window, and says whether its bodies hash as recorded.
//
//     vkm_physics_hash <project> <ticks> <steps a frame> <hash>
//
// Once its world is whole - the clock let go, nothing decoding - it steps the paused clock <steps a
// frame> ticks every <steps a frame>-th frame, so a world that runs anything per frame rather than per
// tick ends elsewhere at 2 or 3 than at 1. At tick <ticks> it hashes every body and joint and exits 0
// when the hash is <hash>, 1 when not. The ctests (tests/CMakeLists.txt) run the lab and the arena, and
// fail on any error a run logs.

#define VKM_LOG_CATEGORY "HASH"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vkm_log/logger.h"

#include "app/engine_app.h"
#include "core/engine.h"
#include "core/frame/arrival_hold.h"
#include "core/frame_context.h"
#include "core/project_boot.h"
#include "core/system.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "io/project/host_boot.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "system/script/module/script_module.h"

using namespace Vkm::Engine;

namespace {

/// FNV-1a over the bytes it is given, in order.
struct Fnv {
    uint64_t hash = 1469598103934665603ull;

    void bytes(const void* data, size_t count) {
        const auto* at = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < count; ++i) {
            hash ^= at[i];
            hash *= 1099511628211ull;
        }
    }

    template <typename T>
    void add(const T& value) { bytes(&value, sizeof(value)); }
};

/// The keys of @p T's storage, lowest first: the hash's order, whatever the storage's.
template <typename T>
std::vector<uint32_t> keysOf(Scene& scene) {
    std::vector<uint32_t> keys;
    if (auto* held = scene.storage<T>()) {
        for (uint32_t i = 0; i < held->size(); ++i) keys.push_back(held->keyAt(i));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

/// Every body's pose, velocities, sleep and support, and every joint's carried impulses.
uint64_t hashOf(Scene& scene) {
    Fnv fnv;
    for (const uint32_t key : keysOf<Rigidbody>(scene)) {
        const EntityId   id   = scene.entityAt(key);
        const Rigidbody& body = scene.get<Rigidbody>(id);
        fnv.add(key);
        fnv.add(id.generation());
        if (const Transform* pose = scene.tryGet<Transform>(id)) {
            fnv.add(pose->position);
            fnv.add(pose->rotation);
        }
        fnv.add(body.linearVelocity);
        fnv.add(body.angularVelocity);
        fnv.add(body.sleeping);
        fnv.add(body.sleepTimer);
        fnv.add(body.supported);
        fnv.add(body.supportNormal);
        fnv.add(body.blockNormal);
        fnv.add(body.supportBody.slot());
    }
    for (const uint32_t key : keysOf<Joint>(scene)) {
        const Joint& joint = scene.get<Joint>(scene.entityAt(key));
        fnv.add(key);
        fnv.add(joint.impulse);
        fnv.add(joint.holdImpulse);
        fnv.add(joint.resolvedDistance);
        fnv.add(joint.heldRotation);
        fnv.add(joint.heldResolved);
    }
    return fnv.hash;
}

/**
 * @brief Steps the world once it is whole, and hashes it at the last tick.
 */
class Hasher : public System {
    public:
        Hasher(int ticks, int steps, uint64_t expected)
            : m_ticks(ticks)
            , m_steps(steps)
            , m_expected(expected)
        {}
        ~Hasher() override = default;

        Hasher(const Hasher& other) = delete;
        Hasher& operator=(const Hasher& other) = delete;

        Hasher(Hasher && other) = delete;
        Hasher& operator=(Hasher && other) = delete;

    public:
        void update(FrameContext& ctx) override {
            ++m_frame;
            if (!m_started) {
                // A world held or still decoding is younger than the one recorded.
                if (ctx.clock.isHeld() || assetsLoading(ctx.resources) > 0) return;
                m_started    = true;
                m_startFrame = m_frame;
                m_startTick  = ctx.clock.getTick();
            }

            const int tick = static_cast<int>(ctx.clock.getTick() - m_startTick);
            if (tick >= m_ticks) {
                const uint64_t hash = hashOf(ctx.scene);
                m_matched = tick == m_ticks && hash == m_expected;
                std::printf(
                    "tick %d hash %016" PRIx64 ", recorded %016" PRIx64 "\n",
                    tick,
                    hash,
                    m_expected
                );
                ctx.window.requestClose();
                return;
            }
            if ((m_frame - m_startFrame) % m_steps == 0) ctx.clock.requestStep(m_steps);
        }

        bool matched() const { return m_matched; }

    private:
        int      m_ticks      = 0;
        int      m_steps      = 1;
        uint64_t m_expected   = 0;
        int      m_frame      = 0;
        int      m_startFrame = 0;
        uint32_t m_startTick  = 0;
        bool     m_started    = false;
        bool     m_matched    = false;
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::printf("Usage: %s <project> <ticks> <steps a frame> <hash>\n", argv[0]);
        return 2;
    }
    const int ticks = std::atoi(argv[2]);
    const int steps = std::max(1, std::atoi(argv[3]));
    const uint64_t expected = std::strtoull(argv[4], nullptr, 16);
    int hostArgc = 2;
    return runHost([&] {
        bootHost(hostArgc, argv, "physics_hash.log", "VKM-HASH");

        ScriptModule module;
        if (!bootGameplayModule(module, "hashing")) return EXIT_FAILURE;

        Project project;
        loadProject(Roots::projectRoot(), project);

        Engine engine;
        Vkm::App::AppConfig config;
        config.windowTitle = project.name.c_str();
        auto systems = Vkm::App::setupEngineApp(engine, config, nullptr);
        engine.getWindow().setFramerate(0);
        if (!bootPlayedWorld(project, engine, systems.behaviors, "hash")) return EXIT_FAILURE;

        Hasher& hasher = engine.addSystem<Hasher>(SystemStage::Editor, ticks, steps, expected);
        engine.getClock().setPaused(true);
        engine.run();
        return hasher.matched() ? EXIT_SUCCESS : EXIT_FAILURE;
    });
}
