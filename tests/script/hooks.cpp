// A behavior's hooks as a game runs them: when each runs, in what order, on which time, and what a
// throw costs.

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "debug/error/engine_error_log.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"
#include "system/script/behavior.h"
#include "system/script/behavior_system.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"

namespace ScriptHooks {

using namespace Vkm::Engine;

/// What the behaviors below did, in order: "<hook> <slot>".
std::vector<std::string>& journal() {
    static std::vector<std::string> s_lines;
    return s_lines;
}

void note(const char* hook, EntityId id) { journal().push_back(
    std::string(hook) + " " + std::to_string(id.slot())
); }

/**
 * @brief Writes each hook that runs into the journal.
 */
class HookDiarist : public Behavior<HookDiarist> {
    public:
        void onStart() override                    { note("start", entity()); }
        void onFixedUpdate(float) override         { note("fixed", entity()); }
        void onUpdate(float) override              { note("update", entity()); }
        void onRealtimeUpdate(float) override      { note("realtime", entity()); }
        void onDestroy() override                  { note("destroy", entity()); }
        void onCollisionEnter(const Collision& hit) override {
            note(("hit by " + std::to_string(hit.other.slot()) + ":").c_str(), entity());
        }
        void onTriggerEnter(EntityId other) override {
            note(("entered by " + std::to_string(other.slot()) + ":").c_str(), entity());
        }
};

/**
 * @brief Throws from its fixed update.
 */
class Fuse : public Behavior<Fuse> {
    public:
        void onFixedUpdate(float) override {
            ++lit;
            throw std::runtime_error("the fuse blew");
        }

    public:
        static inline int lit = 0;
};

/**
 * @brief Sent to the Grumbler.
 */
struct Complaint {};

/**
 * @brief Throws from its listener, and ticks on.
 */
class Grumbler : public Behavior<Grumbler> {
    public:
        void onStart() override {
            subscribe([](const Complaint&) { throw std::runtime_error("the listener blew"); });
        }
        void onFixedUpdate(float) override { ++ticks; }

    public:
        static inline int ticks = 0;
};

/**
 * @brief A pause menu's buttons: asked from the realtime hook, all that runs while the game is paused.
 */
class PauseMenu : public Behavior<PauseMenu> {
    public:
        void onRealtimeUpdate(float) override {
            if (closing) destroy(std::exchange(closing, EntityId{}));
            if (!quitTo.empty()) loadScene(std::exchange(quitTo, std::string()));
        }

    public:
        EntityId    closing;
        std::string quitTo;
};

/**
 * @brief Whether the world was covered, each frame it was updated.
 */
class CoverWatch : public Behavior<CoverWatch> {
    public:
        void onUpdate(float) override { seen.push_back(isCovered()); }

        static inline std::vector<bool> seen;  ///< One a frame, in order.
};

} // namespace ScriptHooks

VKM_BEHAVIOR(::ScriptHooks::HookDiarist)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptHooks::Fuse)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptHooks::Grumbler)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptHooks::PauseMenu)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptHooks::CoverWatch)
VKM_REFLECT_END()

using namespace Vkm::Test;
using namespace ScriptHooks;

namespace {

std::string at(const char* hook, EntityId id) { return std::string(hook) + " " + std::to_string(id.slot()); }

EntityId addBox(Scene& scene, const glm::vec3& position, const glm::vec3& half, RigidbodyMotion motion) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = position;
    scene.add(id, transform);
    Rigidbody body;
    body.motion = motion;
    scene.add(id, body);
    Collider collider;
    collider.parts[0].shape       = ColliderShape::Box;
    collider.parts[0].halfExtents = half;
    scene.add(id, collider);
    return id;
}

/// One frame of @p behaviors alone, as the engine runs it: @p steps ticks, then the frame pass. No steps
/// is a paused frame, which lasts as long as it took.
void frame(Frame& world, BehaviorSystem& behaviors, int steps) {
    world.clock.setPaused(true);
    world.clock.requestStep(steps);
    world.clock.beginFrame();
    while (world.clock.consumeFixedStep()) {
        world.events.flush();
        behaviors.fixedUpdate(world.ctx);
    }
    world.events.flush();
    behaviors.update(world.ctx);
}

} // namespace

TEST("a behavior starts before its first tick's hooks, then runs fixed, update and realtime in slot order, and is destroyed with its entity") {
    Game game;
    Scene& scene = game.scene();
    const EntityId first  = scene.createEntity();
    const EntityId second = scene.createEntity();
    // Given in the opposite order to their slots, so the storage's order is not the slots'.
    addBehavior<HookDiarist>(scene, second);
    addBehavior<HookDiarist>(scene, first);

    game.run(3, [&](int frame) { if (frame == 2) scene.destroyEntity(second); }, 2);

    // The first frame has no realtime pass: it lasts no time, as nothing came before it.
    const std::vector<std::string> expected = {
        at("start", first),    at("start", second),
        at("fixed", first),    at("fixed", second),    at("fixed", first),  at("fixed", second),
        at("update", first),   at("update", second),
        at("fixed", first),    at("fixed", second),    at("fixed", first),  at("fixed", second),
        at("update", first),   at("update", second),
        at("realtime", first), at("realtime", second),
        at("destroy", second),
        at("fixed", first),    at("fixed", first),
        at("update", first),
        at("realtime", first),
        at("destroy", first),  // the engine's end ends the session
    };
    CHECK(journal().size() == expected.size());
    for (size_t i = 0; i < std::min(
        journal().size(),
        expected.size()
    ); ++i) CHECK(journal()[i] == expected[i]);
}

TEST("a collision is heard by the behaviors of both bodies, and a trigger's by the trigger's") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 20.0f}, RigidbodyMotion::Static);
    const EntityId zone  = addBox(scene, {0.0f, 3.0f, 0.0f}, {2.0f, 0.5f, 2.0f}, RigidbodyMotion::Static);
    scene.get<Collider>(zone).isTrigger = true;
    const EntityId crate = addBox(scene, {0.0f, 5.0f, 0.0f}, glm::vec3(0.5f), RigidbodyMotion::Dynamic);
    for (const EntityId id : {floor, zone, crate}) addBehavior<HookDiarist>(scene, id);

    game.run(120);

    const auto heard = [](const std::string& line) {
        int n = 0;
        for (const std::string& seen : journal()) n += seen == line;
        return n;
    };
    const auto slot = [](EntityId id) { return std::to_string(id.slot()); };
    // Every landing heard on both sides; and the trigger, which only senses, heard once.
    const int floorHeard = heard("hit by " + slot(crate) + ": " + slot(floor));
    CHECK(floorHeard >= 1);
    CHECK(heard("hit by " + slot(floor) + ": " + slot(crate)) == floorHeard);
    CHECK(heard("entered by " + slot(crate) + ": " + slot(zone)) == 1);
}

TEST("steps asked while the world is held run when it is let go, with onUpdate and the contacts of their ticks") {
    Game game;
    Scene& scene = game.scene();
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 20.0f}, RigidbodyMotion::Static);
    const EntityId crate = addBox(scene, {0.0f, 1.0f, 0.0f}, glm::vec3(0.5f), RigidbodyMotion::Dynamic);
    addBehavior<HookDiarist>(scene, crate);
    // Programs still building hold the world from the frame after it is said to the frame after it is
    // not: frames 3 to 60 run no tick, and frame 61 takes every step asked meanwhile, the landing among them.
    struct Renderer : System {
        int frame = 0;
        void update(FrameContext& ctx) override {
            ++frame;
            ctx.readiness.programs = 1;
            ctx.readiness.compiled = frame >= 2 && frame < 60 ? 0u : 1u;
        }
    };
    game.engine.addSystem<Renderer>(SystemStage::Editor);

    std::vector<std::string> letGo;
    game.run(61, [&](int frame) {
        if (frame == 60) journal().clear();
        if (frame == 61) letGo = journal();
    });

    const auto heard = [&](const std::string& line) { return std::count(letGo.begin(), letGo.end(), line); };
    CHECK(heard(at("fixed", crate)) == 59);
    CHECK(heard(at("update", crate)) == 1);
    CHECK(heard("hit by " + std::to_string(floor.slot()) + ": " + std::to_string(crate.slot())) >= 1);
}

TEST("while the game is paused onUpdate and onFixedUpdate wait, onRealtimeUpdate runs, and nothing new starts") {
    Frame world;
    BehaviorSystem behaviors;
    behaviors.init(world.ctx);
    const EntityId early = world.scene.createEntity();
    const EntityId late  = world.scene.createEntity();
    addBehavior<HookDiarist>(world.scene, early);
    frame(world, behaviors, 1);
    frame(world, behaviors, 1);

    journal().clear();
    // One spawned while paused starts only when time flows again.
    addBehavior<HookDiarist>(world.scene, late);
    for (int i = 0; i < 3; ++i) frame(world, behaviors, 0);
    const std::vector<std::string> paused = journal();

    journal().clear();
    frame(world, behaviors, 1);
    const std::vector<std::string> resumed = journal();
    behaviors.shutdown();

    CHECK(paused == std::vector<std::string>(3, at("realtime", early)));
    const std::vector<std::string> expected = {
        at("start", late),  at("fixed", early),  at("fixed", late),
        at("update", early), at("update", late), at("realtime", early), at("realtime", late),
    };
    CHECK(resumed == expected);
}

TEST("while the game is paused, a destroy or a scene load asked from onRealtimeUpdate still happens, and the load is warned of") {
    openProject();
    {
        Frame author;
        author.scene.add(author.scene.createEntity(), makeName("Title"));
        REQUIRE(
            SceneSerializer::save(author.scene, author.resources, (Roots::scenes() / "title.json").string())
        );
    }
    Frame world;
    BehaviorSystem behaviors;
    behaviors.init(world.ctx);
    const EntityId popup = world.scene.createEntity();
    PauseMenu& buttons   = addBehavior<PauseMenu>(world.scene, world.scene.createEntity());
    // The first frame lasts no time, so has no realtime pass.
    frame(world, behaviors, 1);
    frame(world, behaviors, 1);

    buttons.closing = popup;
    frame(world, behaviors, 0);
    const bool closed = !world.scene.isAlive(popup);
    buttons.quitTo = "scenes/title.json";
    frame(world, behaviors, 0);
    behaviors.shutdown();

    CHECK(closed);
    CHECK(findLowestSlot<Name>(
        world.scene,
        [](const Name& name) { return std::string(name.value) == "Title"; }
    ));
    const auto* scripts = world.scene.storage<ScriptComponent>();
    CHECK((!scripts || scripts->size() == 0u));
    CHECK(countInLog("was loaded while simulation time is frozen") == 1);
}

TEST("a contact heard before the game pauses is dropped, never delivered when it resumes") {
    Frame world;
    BehaviorSystem behaviors;
    behaviors.init(world.ctx);
    const EntityId a = world.scene.createEntity();
    const EntityId b = world.scene.createEntity();
    addBehavior<HookDiarist>(world.scene, a);
    addBehavior<HookDiarist>(world.scene, b);
    frame(world, behaviors, 1);
    const auto hits = [] {
        int n = 0;
        for (const std::string& line : journal()) n += line.rfind("hit by", 0) == 0;
        return n;
    };

    // Delivered to both on a frame that ticks...
    world.events.enqueue(CollisionEvent{a, b});
    frame(world, behaviors, 1);
    const int running = hits();
    // ...and dropped by one that does not: physics did not run, so it tells of a world gone by.
    world.events.enqueue(CollisionEvent{a, b});
    frame(world, behaviors, 0);
    frame(world, behaviors, 1);
    behaviors.shutdown();

    CHECK(running == 2);
    CHECK(hits() == 2);
}

TEST("a throwing hook is reported once and turns its behavior off, a throwing listener only reports, and the rest run on") {
    EngineErrorLog errors;
    setErrorSink(&errors);
    Game game;
    Scene& scene = game.scene();
    addBehavior<Fuse>(scene, scene.createEntity());
    addBehavior<Grumbler>(scene, scene.createEntity());
    const EntityId steady = scene.createEntity();
    addBehavior<HookDiarist>(scene, steady);

    game.run(10, [&](int frame) { if (frame == 5) game.events().emit(Complaint{}); });
    setErrorSink(nullptr);

    int fixedSteady = 0;
    for (const std::string& line : journal()) fixedSteady += line == at("fixed", steady);
    CHECK(Fuse::lit == 1);
    CHECK(Grumbler::ticks == 10);
    CHECK(fixedSteady == 10);
    REQUIRE(errors.entries().size() == 2);
    CHECK(errors.entries()[0].category == "Behavior");
    CHECK(errors.entries()[0].source == "Fuse / onFixedUpdate");
    CHECK(errors.entries()[0].message == "the fuse blew");
    CHECK(errors.entries()[0].repeatCount == 1u);
    CHECK(errors.entries()[1].source == "Grumbler / event listener");
    CHECK(errors.entries()[1].message == "the listener blew");
}

TEST("a behavior is told the world is covered while the splash shows, and that it is not once it has gone") {
    Game game;
    addBehavior<CoverWatch>(game.scene(), game.scene().createEntity());
    CoverWatch::seen.clear();
    // Frames of a quarter second: the engine's ident, every host's splash, lasts IDENT_SECONDS.
    game.run(60, {}, 15);
    const std::vector<bool>& seen = CoverWatch::seen;
    REQUIRE(seen.size() == 60u);
    CHECK(seen.front());
    CHECK(!seen.back());
    // Covered, then not: once lifted it stays lifted.
    CHECK(std::is_partitioned(seen.begin(), seen.end(), [](bool covered) { return covered; }));
}
