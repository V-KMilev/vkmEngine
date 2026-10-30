// The staged frame, as a game's systems see it: the order they run in, what each finds from the
// stages before it, and when the world is let tick.

#include <functional>
#include <string>
#include <vector>

#include "ecs/component/landscape/water.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/hierarchy_operations.h"
#include "io/asset/generate/mesh_generators.h"
#include "resource/async_load_queue.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

namespace {

struct Tock {
    uint32_t tick = 0;
};

/**
 * @brief Writes down when it is called, and what the frame holds when it is.
 */
class Recorder : public System {
    public:
        Recorder(std::vector<std::string>& log, std::string name) : m_log(log), m_name(std::move(name)) {}

        void init(FrameContext&) override { m_log.push_back(m_name + " init"); }
        void fixedUpdate(FrameContext&) override { m_log.push_back(m_name + " tick"); }
        void update(FrameContext& ctx) override {
            m_log.push_back(m_name + " frame");
            sawVisibility = ctx.visibility != nullptr;
            if (look) look(ctx);
        }
        void shutdown() override { m_log.push_back(m_name + " shutdown"); }

        bool                               sawVisibility = false;
        std::function<void(FrameContext&)> look;

    private:
        std::vector<std::string>& m_log;
        std::string               m_name;
};

/// A mesh added as a decode in flight adds it: loading, empty until AsyncLoaderSystem lands one.
MeshHandle addDecoding(ResourceManager& resources, const std::string& name) {
    MeshAsset stub;
    stub.loading = true;
    return resources.add(std::move(stub), name);
}

/// The decode of @p mesh done: a plane 20 m across, for AsyncLoaderSystem to land in the next frame.
void decoded(ResourceManager& resources, MeshHandle mesh) {
    AsyncLoadQueue::get().pushMesh({mesh, resources.get(mesh).uid(), generatePlane({20.0f, 20.0f})});
}

/**
 * @brief In tick TICK, after the physics, a floor of a mesh still decoding and a crate thrown down at
 *        it two meters a tick, as a game spawns them; then the crate's height after every tick.
 */
class Thrower : public System {
    public:
        static constexpr uint32_t TICK = 3;  ///< At two ticks a frame, the first of the second frame.

        void fixedUpdate(FrameContext& ctx) override {
            const uint32_t tick = ctx.clock.getTick();
            if (tick > TICK) heights.push_back(positionOf(ctx.scene, crate).y);
            if (tick != TICK) return;

            floor = addDecoding(ctx.resources, "test:floor");
            ColliderPart part;
            part.shape = ColliderShape::Mesh;
            part.mesh  = floor;
            addBody(ctx.scene, glm::vec3(0.0f), part, RigidbodyMotion::Static);
            crate = addCrate(ctx.scene, {0.0f, 0.52f, 0.0f});
            bodyOf(ctx.scene, crate).linearVelocity = {0.0f, -120.0f, 0.0f};
        }

        MeshHandle         floor;
        EntityId           crate;
        std::vector<float> heights;
};

} // namespace

TEST("the engine runs its stages in order, each seeing what the stages before it made") {
    Game game;
    std::vector<std::string> log;
    // Registered last stage first: the order seen is the stages', not the registration's.
    const char* names[]    = {"input", "simulation", "transform", "visibility", "render", "editor"};
    Recorder* recorders[6] = {};
    for (int stage = 5; stage >= 0; --stage) {
        recorders[stage] = &game.engine.addSystem<Recorder>(
            static_cast<SystemStage>(stage),
            log,
            names[stage]
        );
    }

    // What a tick enqueues in the Input stage is heard before the Simulation stage runs that tick.
    std::vector<uint32_t> heard, heardBySimulation;
    game.events().subscribe<Tock>([&](const Tock& t) { heard.push_back(t.tick); });
    struct Ticker : System {
        void fixedUpdate(FrameContext& ctx) override { ctx.events.enqueue(Tock{ctx.clock.getTick()}); }
    };
    struct Hearer : System {
        std::vector<uint32_t>& heard;
        std::vector<uint32_t>& seen;
        Hearer(std::vector<uint32_t>& h, std::vector<uint32_t>& s) : heard(h), seen(s) {}
        void fixedUpdate(FrameContext&) override { seen.push_back(heard.empty() ? 0u : heard.back()); }
    };
    game.engine.addSystem<Ticker>(SystemStage::Input);
    game.engine.addSystem<Hearer>(SystemStage::Simulation, heard, heardBySimulation);

    // A parent the Input stage moves each frame: the Transform stage, after the hierarchy's
    // resolve, finds its child where the move put it that same frame.
    Scene& scene = game.scene();
    const EntityId parent = scene.createEntity();
    const EntityId child  = scene.createEntity();
    scene.add(parent, Transform{});
    Transform offset;
    offset.position = {0.0f, 1.0f, 0.0f};
    scene.add(child, offset);
    HierarchyOperations::setParent(scene, child, parent);
    recorders[0]->look = [&](FrameContext& ctx) {
        ctx.scene.get<Transform>(parent).position.x += 1.0f;
    };
    std::vector<float> childX;
    recorders[2]->look = [&](FrameContext& ctx) {
        childX.push_back(ctx.scene.get<WorldTransform>(child).model[3].x);
    };

    game.run(2, {}, 2);

    std::vector<std::string> expected;
    for (const char* name : names) expected.push_back(std::string(name) + " init");
    for (int frame = 0; frame < 2; ++frame) {
        for (int tick = 0; tick < 2; ++tick) {
            for (const char* name : names) expected.push_back(std::string(name) + " tick");
        }
        for (const char* name : names) expected.push_back(std::string(name) + " frame");
    }
    for (int stage = 5; stage >= 0; --stage) expected.push_back(std::string(names[stage]) + " shutdown");
    CHECK(log == expected);

    CHECK(!recorders[1]->sawVisibility);
    CHECK(recorders[4]->sawVisibility);
    CHECK(recorders[5]->sawVisibility);
    CHECK(heardBySimulation == std::vector<uint32_t>{1, 2, 3, 4});
    CHECK(childX == std::vector<float>{1.0f, 2.0f});
}

TEST("no tick runs while the frame's programs are still being built, and the world starts once they are") {
    Game game;
    // Written after the frame is drawn, as the renderer says how far it is: the next frame reads it.
    struct Renderer : System {
        int frame = 0;
        void update(FrameContext& ctx) override {
            ++frame;
            ctx.readiness.programs = 3;
            ctx.readiness.compiled = frame < 4 ? 1u : 3u;
        }
    };
    game.engine.addSystem<Renderer>(SystemStage::Editor);

    std::vector<uint32_t> ticks;
    game.run(7, [&](int) { ticks.push_back(game.clock().getTick()); }, 2);

    // Frame 1 ran before anything said it was compiling; frames 2 to 4 read "compiling" and run
    // none; frame 5 reads "built" and takes the steps asked meanwhile, which waited.
    CHECK(ticks == std::vector<uint32_t>{2, 2, 2, 2, 10, 12, 14});
}

TEST("a mesh collider made in play while its mesh decodes stops a crate the tick after, however long the decode takes") {
    // The floor's decode lands @p frames frames after the frame it was made in. Its tick and the next
    // fall in one frame: the next waits for the floor all the same.
    const auto play = [](int frames) {
        Game game;
        Thrower& thrower = game.engine.addSystem<Thrower>(SystemStage::Simulation);
        game.run(16, [&](int frame) {
            if (frame == 2 + frames) decoded(game.resources(), thrower.floor);
        }, 2);
        return thrower.heights;
    };
    const std::vector<float> soon = play(1);
    const std::vector<float> late = play(8);

    // Every tick asked ran in the end, the first after the throw meeting the floor.
    REQUIRE(soon.size() == 32u - Thrower::TICK);
    CHECK_NEAR(soon.front(), 0.5f, 0.01f);
    CHECK_NEAR(soon.back(), 0.5f, 0.01f);
    CHECK(late == soon);
}

TEST("a mesh only drawn holds no tick while it decodes, and the same mesh made water holds them until it lands") {
    Game game;
    MeshHandle pond;
    EntityId   entity;
    std::vector<uint32_t> ticks;
    game.run(8, [&](int frame) {
        if (frame == 1) {
            pond   = addDecoding(game.resources(), "test:pond");
            entity = game.scene().createEntity();
            game.scene().add(entity, Transform{});
            Mesh mesh;
            mesh.mesh = pond;
            game.scene().add(entity, mesh);
        }
        if (frame == 3) game.scene().add(entity, Water{});
        if (frame == 5) decoded(game.resources(), pond);
        ticks.push_back(game.clock().getTick());
    }, 2);

    // Drawn alone it decodes through frames 2 and 3, which tick; as water it holds frames 4 to 6, the
    // last landing it; frame 7 runs the ticks asked meanwhile.
    CHECK(ticks == std::vector<uint32_t>{2, 4, 6, 6, 6, 6, 14, 16});
}

TEST("an unset host chrome is the whole window with nothing captured, and a cover is asked once") {
    Frame frame;
    const HostChrome& chrome = frame.ctx.chrome;
    const HostChrome::ViewportRect whole = chrome.viewportIn(frame.window);
    CHECK(whole.x == 0u);
    CHECK(whole.y == 0u);
    CHECK(whole.width == frame.window.getWidth());
    CHECK(whole.height == frame.window.getHeight());
    CHECK(!chrome.capturePointer);
    CHECK(!chrome.captureKeyboard);
    CHECK(!chrome.authoring);
    CHECK(!chrome.effectPreview.playing);
    CHECK(chrome.hiddenInView.empty());
    CHECK(chrome.display.mode == RenderMode::Lit);
    CHECK(!chrome.display.gridShown());

    // A rect the host declares is the rect.
    frame.chrome.viewport = {10, 20, 300, 200};
    const HostChrome::ViewportRect declared = frame.chrome.viewportIn(frame.window);
    CHECK(declared.x == 10u);
    CHECK(declared.y == 20u);
    CHECK(declared.width == 300u);
    CHECK(declared.height == 200u);

    CHECK(!frame.chrome.takeCoverNextWorld());
    frame.chrome.coverNextWorld = true;
    CHECK(frame.chrome.takeCoverNextWorld());
    CHECK(!frame.chrome.takeCoverNextWorld());
}
