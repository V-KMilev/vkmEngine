#include "physics/physics_support.h"
#include "net/net_support.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

#if defined(__linux__)
#include <dlfcn.h>
#endif

#include "core/event/event_bus.h"
#include "debug/engine_error_log.h"
#include "ecs/component/core/world_transform.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "platform/library/dynamic_library.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset_ref.h"
#include "resource/generate/mesh_generators.h"
#include "system/audio/audio_events.h"
#include "system/loading/loading_system.h"
#include "system/physics/physics_system.h"
#include "system/script/behavior.h"
#include "system/script/behavior_registry.h"
#include "system/script/behavior_system.h"
#include "system/script/reflected_behavior.h"
#include "system/script/script_component.h"
#include "system/script/script_module.h"

namespace {

// Records every hook it is given, so a test asserts the contract in
// docs/reference/scripting.md ("Time and pause") rather than its implementation.
class Recorder : public ReflectedBehavior<Recorder> {
    public:
        void onStart() override          { ++starts; }
        void onUpdate(float dt) override {
            ++updates;
            lastSimDelta = dt;
        }
        void onRealtimeUpdate(float dt) override {
            ++realtimeUpdates;
            lastRealDelta = dt;
        }
        void onFixedUpdate(float dt) override {
            ++fixedUpdates;
            lastFixedDelta = dt;
        }
        void onDestroy() override        { ++destroys; }

    public:
        // Counters, not authored fields: unreflected, so they neither serialize nor
        // survive a clone, as the clone test checks.
        int   starts          = 0;
        int   updates         = 0;
        int   realtimeUpdates = 0;
        int   fixedUpdates    = 0;
        int   destroys        = 0;
        float lastSimDelta    = 0.0f;
        float lastRealDelta   = 0.0f;
        float lastFixedDelta  = 0.0f;

        float speed = 1.0f;   ///< The one authored field, reflected below.
};

// One authored field of each scalar kind a behavior can declare.
class Tuned : public ReflectedBehavior<Tuned> {
    public:
        float       speed = 1.0f;
        int         lives = 3;
        bool        armed = true;
        std::string label = "start";
};

// An event type a game declares: its bus is built from the module's own code.
struct ModuleEvent { int value = 0; };

// Whether a T can register a listener for ModuleEvent.
template<typename T, typename = void>
struct CanSubscribe : std::false_type {};

template<typename T>
struct CanSubscribe<T, std::void_t<decltype(
    std::declval<T&>().template subscribe<ModuleEvent>(std::function<void(const ModuleEvent&)>{})
)>> : std::true_type {};

// A listener subscribed straight on the bus is nobody's to drop and outlives the
// module whose code it calls, so a behavior's bus sends but cannot subscribe:
// Behavior::subscribe, tracked and dropped, is the only way in. Checked at compile
// time, inside a member function where the behavior is a complete type.
class Sender : public Behavior {
        static void holdsTheRule() {
            static_assert(CanSubscribe<EventBus>::value, "the detector recognises a bus");
            static_assert(
                !CanSubscribe<decltype(std::declval<Sender&>().events())>::value,
                "a behavior's events() can emit, never subscribe"
            );
        }
};

// Listens for one, as docs/reference/events.md tells a game to.
class Listener : public ReflectedBehavior<Listener> {
    public:
        void onStart() override {
            subscribe([this](const ModuleEvent& e) { heard += e.value; });
        }

    public:
        int heard = 0;
};

// Spawns an entity in onStart and destroys its own in a later update - the two
// structural edits a hook may make.
class Gardener : public ReflectedBehavior<Gardener> {
    public:
        void onStart() override { planted = spawn("Planted"); }

        void onUpdate(float) override {
            ++ticks;
            if (ticks == 2) destroy();
        }

    public:
        EntityId planted{};
        int      ticks = 0;
};

// Builds a subtree in onStart through the parented spawn overload, as UI or a rig
// built in code does.
class Builder : public ReflectedBehavior<Builder> {
    public:
        void onStart() override {
            root  = spawn("Root");
            left  = spawn("Left",  root);
            right = spawn("Right", root);
        }

    public:
        EntityId root{};
        EntityId left{};
        EntityId right{};
};

// Plays a sound every fixed tick: from itself, or at a point.
class Chimer : public ReflectedBehavior<Chimer> {
    public:
        void onFixedUpdate(float) override {
            if (atPoint) playSoundAt(chime, glm::vec3(1.0f, 2.0f, 3.0f));
            else         playSound(chime);
        }

    public:
        AssetRef<AudioClipAsset> chime;
        bool                     atPoint = false;
};

// Lifts its parent a metre each fixed tick and reads where that leaves it.
class Rider : public ReflectedBehavior<Rider> {
    public:
        void onFixedUpdate(float) override {
            const EntityId parent = get<Hierarchy>().parent;
            scene().get<Transform>(parent).position.y += 1.0f;
            seen = worldPosition();
        }

    public:
        glm::vec3 seen{0.0f};
};

// Asks, only while paused, for a scene that does not exist: the request is under
// test, not the load, and the drain is seen through the session-end onDestroy.
class Quitter : public ReflectedBehavior<Quitter> {
    public:
        void onRealtimeUpdate(float) override {
            if (clock().isPaused()) loadScene("scenes/nowhere.json");
        }
        void onDestroy() override { ++destroys; }

    public:
        int destroys = 0;
};

// Asks once, on its first update, for the scene being played to start over.
class Restarter : public ReflectedBehavior<Restarter> {
    public:
        void onUpdate(float) override {
            if (!asked) restartScene();
            asked = true;
        }

    public:
        bool asked = false;
};

// Goes on to scenes/two.json; there, asks for one that is missing, then for the scene to start over.
class Traveller : public ReflectedBehavior<Traveller> {
    public:
        void onUpdate(float) override {
            if (step == 0) loadScene("scenes/two.json");
            if (step == 1) loadScene("scenes/missing.json");
            if (step == 2) restartScene();
            ++step;
        }

    public:
        int step = 0;
};

// Asks, every update, for a scene of its own.
class Wanderer : public ReflectedBehavior<Wanderer> {
    public:
        void onUpdate(float) override { loadScene("scenes/own.json"); }
};

// A settings screen: a tier picked each update, built over the project's settings.
class Tiers : public ReflectedBehavior<Tiers> {
    public:
        void onUpdate(float) override { render() = withPreset(projectRender(), preset); }

    public:
        GraphicsPreset preset = GraphicsPreset::Low;
};

// Takes another entity with it when it goes, through the deferred queue.
class Partner : public ReflectedBehavior<Partner> {
    public:
        void onDestroy() override { destroy(partner); }

    public:
        EntityId partner{};
};

class Thrower : public ReflectedBehavior<Thrower> {
    public:
        void onUpdate(float) override {
            ++attempts;
            throw std::runtime_error("a behavior that throws is disabled, not fatal");
        }

    public:
        int attempts = 0;
};

// Names a sound as a game's behavior does: an authored field holding the asset's name,
// no handle behind it.
class Singer : public ReflectedBehavior<Singer> {
    public:
        AssetRef<AudioClipAsset> song;
};

// Names a prefab it builds, as a behavior that throws or explodes does.
class Launcher : public ReflectedBehavior<Launcher> {
    public:
        PrefabRef shell;
};

// Names a material by its name alone, as a behavior that spawns decals does.
class Painter : public ReflectedBehavior<Painter> {
    public:
        AssetRef<MaterialAsset> paint;
};

// Throws from an event listener, reaching gameplay through the bus rather than
// BehaviorSystem.
class EventThrower : public ReflectedBehavior<EventThrower> {
    public:
        void onStart() override {
            subscribe([this](const ModuleEvent&) {
                ++attempts;
                throw std::runtime_error("a listener that throws is reported, not fatal");
            });
        }

    public:
        int attempts = 0;
};

// Logs every contact hook into a log the test owns, so it outlives the entity.
class Toucher : public ReflectedBehavior<Toucher> {
    public:
        struct Heard {
            const char* hook;
            EntityId    entity;
            EntityId    other;
            glm::vec3   normal;
        };

        void onCollisionEnter(const Collision& hit) override { note("enter", hit); }
        void onCollisionStay(const Collision& hit) override  { note("stay", hit); }
        void onCollisionExit(const Collision& hit) override  { note("exit", hit); }

    private:
        void note(const char* hook, const Collision& hit) {
            if (log) log->push_back({hook, entity(), hit.other, hit.normal});
        }

    public:
        std::vector<Heard>* log = nullptr;
};

// One authored field of each vector kind a behavior can declare.
class Shaped : public ReflectedBehavior<Shaped> {
    public:
        glm::vec2 offset    = {0.0f, 0.0f};
        glm::vec4 tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
        glm::quat facing    = {1.0f, 0.0f, 0.0f, 0.0f};
};

// Finds behaviors from inside one, as a game reaches another entity's.
class Seeker : public ReflectedBehavior<Seeker> {
    public:
        void onStart() override;

    public:
        EntityId  target{};
        Recorder* found = nullptr;
        Seeker*   self  = nullptr;
        Recorder* first = nullptr;  ///< The scene's lowest-slot Recorder.
};

// One behavior calling another, as gameplay wires them: the one called stands later in
// the tick's order, yet is already bound and started when the first one's hook runs.
class Callee : public ReflectedBehavior<Callee> {
    public:
        void onStart() override { started = true; }
        bool seesItsEntity() const { return tryGet<Transform>() != nullptr; }

    public:
        bool started = false;
};

class Caller : public ReflectedBehavior<Caller> {
    public:
        void onStart() override { calleeBoundAtStart = callee && callee->seesItsEntity(); }
        void onUpdate(float) override {
            if (!callee) return;
            calleeStarted = callee->started;
            calleeBound   = callee->seesItsEntity();
        }

    public:
        Callee* callee             = nullptr;
        bool    calleeStarted      = false;
        bool    calleeBound        = false;
        bool    calleeBoundAtStart = false;
};

} // namespace

VKM_REFLECT_BEGIN(Callee)
VKM_REFLECT_END()
VKM_REFLECT_BEGIN(Caller)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Recorder)
    VKM_F(speed)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Gardener)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Tuned)
    VKM_F(speed)
    VKM_F(lives)
    VKM_F(armed)
    VKM_F(label)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Traveller)
    VKM_F(step)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Listener)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Toucher)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Builder)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Rider)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Chimer)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Quitter)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Restarter)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Wanderer)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Partner)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Tiers)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Thrower)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Singer)
    VKM_F(song)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Painter)
    VKM_F(paint)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Launcher)
    VKM_F(shell)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(EventThrower)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Seeker)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Shaped)
    VKM_F(offset)
    VKM_F(tintColor)
    VKM_F(facing)
VKM_REFLECT_END()

namespace {

void Seeker::onStart() {
    found = findBehavior<Recorder>(target);
    self  = findBehavior<Seeker>();
    first = findInScene<Recorder>();
}

template <typename T>
T& attach(Scene& scene, EntityId entity) {
    auto instance = std::make_unique<T>();
    T& behavior = *instance;
    ScriptComponent script;
    script.behaviors.push_back(std::move(instance));
    scene.add(entity, std::move(script));
    return behavior;
}

// A frame as Engine::run drives one: the clock advances, then the system updates.
// The tick sleeps because Clock measures a real span: its first beginFrame() reports
// zero, two in a row on a fast machine round to it, and a zero-time frame is one the
// engine never has.
struct ScriptFrame {
    TestFrame      frame;
    BehaviorSystem system;

    explicit ScriptFrame(Scene& scene) : frame(scene) {
        system.init(frame.ctx);
        // Starts the clock, so the first measured frame is the first tick().
        frame.clock.beginFrame();
    }

    ~ScriptFrame() { system.shutdown(); }

    void tick() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        frame.clock.beginFrame();
        system.update(frame.ctx);
    }

    // No span needed: a fixed step is a length the project declares, not a measured one.
    void fixed() { system.fixedUpdate(frame.ctx); }

    void pause(bool paused) { frame.clock.setPaused(paused); }
};

void testStartHappensOnSimulationTime() {
    std::printf("When a behavior starts:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Recorder& recorder = attach<Recorder>(scene, entity);

    ScriptFrame run(scene);
    // Paused is Edit mode, where an open scene runs nothing, so the realtime pass must
    // not begin a session.
    run.pause(true);
    run.tick();
    check("a paused frame starts nothing", recorder.starts == 0);
    check("  and no realtime hook runs on an unstarted behavior", recorder.realtimeUpdates == 0);

    run.pause(false);
    run.tick();
    check("the first simulation tick starts it", recorder.starts == 1);
    check("  and updates it in the same tick", recorder.updates == 1);
    check("  and the realtime hook runs once it has started", recorder.realtimeUpdates == 1);
    check("simulation delta is positive when it runs at all", recorder.lastSimDelta > 0.0f);

    run.tick();
    check("starting happens once, not once a frame", recorder.starts == 1);
}

// A tier is built over the project's settings, which the session's never overwrite: High after
// Low brings back what Low turned off.
void testATierIsBuiltOverTheProjectsSettings() {
    std::printf("A settings screen picking a quality tier:\n");

    Scene scene;
    Tiers& tiers = attach<Tiers>(scene, scene.createEntity());
    ScriptFrame run(scene);
    run.frame.projectRender.bloom = true;
    run.frame.render              = run.frame.projectRender;

    run.tick();
    check("Low turns bloom off", !run.frame.render.bloom);
    tiers.preset = GraphicsPreset::High;
    run.tick();
    check("  and High after it brings it back", run.frame.render.bloom);
    check("  the project's own left as it was", run.frame.projectRender.bloom);
}

void testEveryBehaviorStartsBeforeAnyHookRuns() {
    std::printf("A behavior calling one later in the order:\n");

    Scene scene;
    const EntityId first  = scene.createEntity();
    const EntityId second = scene.createEntity();
    scene.add(second, Transform{});
    Caller& caller = attach<Caller>(scene, first);
    caller.callee  = &attach<Callee>(scene, second);

    ScriptFrame run(scene);
    run.tick();
    check("from the caller's onStart, the one called is already bound", caller.calleeBoundAtStart);
    check("the one called has started before the caller's first hook", caller.calleeStarted);
    check("  and is bound to its entity", caller.calleeBound);
}

void testPauseSplitsTheTwoTimelines() {
    std::printf("What a pause stops and what it does not:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Recorder& recorder = attach<Recorder>(scene, entity);

    ScriptFrame run(scene);
    run.tick();
    const int updatesBefore  = recorder.updates;
    const int realtimeBefore = recorder.realtimeUpdates;

    run.pause(true);
    for (int i = 0; i < 3; ++i) {
        run.tick();
    }

    check("onUpdate does not run at all while paused", recorder.updates == updatesBefore);
    check("onRealtimeUpdate runs every frame regardless", recorder.realtimeUpdates == realtimeBefore + 3);
    check("  on the real delta, which a pause does not scale", recorder.lastRealDelta > 0.0f);

    // Time scale reaches the simulation timeline, not the real one: a zero scale is a
    // pause the game asked for.
    run.pause(false);
    run.frame.clock.setTimeScale(0.0f);
    const int updatesAtScaleZero = recorder.updates;
    run.tick();
    check("a zero time scale stops onUpdate too", recorder.updates == updatesAtScaleZero);
    check("  and still not onRealtimeUpdate", recorder.realtimeUpdates == realtimeBefore + 4);
}

void testFixedUpdateRunsOnTheFixedStep() {
    std::printf("What a fixed update is given:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Recorder& recorder = attach<Recorder>(scene, entity);

    ScriptFrame run(scene);
    run.fixed();

    check("a fixed tick starts an instance that has not run yet", recorder.starts == 1);
    check(
        "  and gives it the fixed step, not the frame delta",
        nearly(recorder.lastFixedDelta, run.frame.clock.getFixedStep())
    );
    check("  once, for the one fixed tick", recorder.fixedUpdates == 1);
}

void testSpawnAndDeferredDestroy() {
    std::printf("Structural edits from inside a hook:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Gardener& gardener = attach<Gardener>(scene, entity);

    ScriptFrame run(scene);
    run.tick();

    check("spawn() from onStart makes an entity", scene.isAlive(gardener.planted));
    check(
        "  carrying the name it was given",
        scene.has<Name>(gardener.planted) && std::string(scene.get<Name>(gardener.planted).value) == "Planted"
    );
    check("the spawning entity survives its own tick", scene.isAlive(entity));

    // The second tick destroys the behavior's own entity, deferred so the hook returns
    // into a live object. That frees `gardener` too, so its planted id is read first.
    const EntityId planted = gardener.planted;
    run.tick();
    check("destroy() of your own entity takes effect after the pass", !scene.isAlive(entity));
    check("  and leaves what it spawned alone", scene.isAlive(planted));
}

void testBehaviorsAreFoundAndAddedByType() {
    std::printf("Finding and adding behaviors by type:\n");

    BehaviorRegistry& registry = BehaviorRegistry::get();
    registry.registerBehaviors<Recorder, Tuned, Seeker>();
    check(
        "registerBehaviors registers each type it names",
        registry.contains("Recorder") && registry.contains("Tuned") && registry.contains("Seeker")
    );

    Scene scene;
    const EntityId first  = scene.createEntity();
    const EntityId second = scene.createEntity();
    const EntityId third  = scene.createEntity();

    // Added last-slot first, so a storage-order walk would meet it first.
    Recorder& late = addBehavior<Recorder>(scene, third);
    check("addBehavior gives an entity its ScriptComponent", scene.has<ScriptComponent>(third));
    Recorder& early = addBehavior<Recorder>(scene, first);
    Tuned&    tuned = addBehavior<Tuned>(scene, first);
    check("  and adds to one it already has", scene.get<ScriptComponent>(first).behaviors.size() == 2);

    check("findBehavior answers the one of that type", findBehavior<Tuned>(scene, first) == &tuned);
    check("  and the first of several kinds", findBehavior<Recorder>(scene, first) == &early);
    check("  and null where there is none", findBehavior<Recorder>(scene, second) == nullptr);
    check(
        "findEntityWithBehavior answers the lowest slot carrying one",
        findEntityWithBehavior<Recorder>(scene) == first
    );
    check("  and a null id when nothing does", !findEntityWithBehavior<Seeker>(scene));
    check("findInScene answers the lowest slot's", findInScene<Recorder>(scene) == &early);
    check("  and null when nothing carries one", findInScene<Seeker>(scene) == nullptr);
    std::vector<EntityId> walked;
    forEachBehavior<Recorder>(scene, [&](EntityId id, Recorder&) { walked.push_back(id); });
    check(
        "forEachBehavior meets every entity carrying one, once, lowest slot first",
        walked == std::vector<EntityId>{first, third}
    );
    std::vector<EntityId> outer;
    size_t                pairs = 0;
    forEachBehavior<Recorder>(scene, [&](EntityId id, Recorder&) {
        forEachBehavior<Recorder>(scene, [&](EntityId, Recorder&) { ++pairs; });
        outer.push_back(id);
    });
    check("  and a walk inside a walk leaves the outer one whole", pairs == 4 && outer == walked);
    Name& named = scene.getOrAdd<Name>(second);
    check("getOrAdd gives an entity the component it lacks", scene.tryGet<Name>(second) == &named);
    check("  and answers the one it has", &scene.getOrAdd<Name>(second) == &named);

    Seeker& seeker = addBehavior<Seeker>(scene, second);
    seeker.target = third;

    ScriptFrame run(scene);
    run.tick();
    check("a behavior added in code starts on the next tick", early.starts == 1 && late.starts == 1);
    check("a behavior finds another entity's by type", seeker.found == &late);
    check("  and its own entity's", seeker.self == &seeker);
    check("  and the scene's lowest-slot one", seeker.first == &early);

    // Added mid-session from outside the hooks: the same lazy start a load gives.
    Recorder& added = addBehavior<Recorder>(scene, second);
    check("  one added mid-session has not started yet", added.starts == 0);
    run.tick();
    check("  and starts on the next tick", added.starts == 1 && added.updates == 1);

    registry.clear();
}

void testSpawnUnderAParent() {
    std::printf("Spawning into a hierarchy:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Builder& builder = attach<Builder>(scene, entity);

    ScriptFrame run(scene);
    run.tick();

    check(
        "the parented overload makes the entity",
        scene.isAlive(builder.left) && scene.isAlive(builder.right)
    );
    check(
        "  carrying the name it was given",
        scene.has<Name>(builder.left) && std::string(scene.get<Name>(builder.left).value) == "Left"
    );

    // HierarchyOperations::setParent underneath, so the child is reachable by the same
    // walk as everything else, under the named parent rather than the behavior's entity.
    check("  under the parent it was given", childrenOf(scene, builder.root) == 2);
    check("  and not under the entity that spawned it", childrenOf(scene, entity) == 0);
    check(
        "  with the parent link pointing back",
        scene.has<Hierarchy>(builder.left) && scene.get<Hierarchy>(builder.left).parent == builder.root
    );

    // The unparented overload skips that last step, so its spawn is a root.
    check(
        "while an unparented spawn is a root",
        !scene.has<Hierarchy>(builder.root) || !scene.get<Hierarchy>(builder.root).parent
    );
}

// A tick moves things the Transform stage resolves only once a frame, and a frame can run several
// ticks: where a behavior is reads the Transforms as they stand.
void testWhereABehaviorIsReadsThisTicksMoves() {
    std::printf("Where a behavior is, inside a tick:\n");

    Scene scene;
    const EntityId parent = scene.createEntity();
    scene.add<Transform>(parent, Transform{});
    const EntityId entity = scene.createEntity();
    Transform at;
    at.position = {1.0f, 0.0f, 0.0f};
    scene.add<Transform>(entity, std::move(at));
    scene.add<WorldTransform>(entity, WorldTransform{});  // the last resolve: at the origin
    HierarchyOperations::setParent(scene, entity, parent);
    Rider& rider = attach<Rider>(scene, entity);

    ScriptFrame run(scene);
    run.fixed();
    check("it is where its parent's move this tick put it", glm::distance(rider.seen, {1, 1, 0}) < 1e-5f);
    run.fixed();
    check("  and the next tick's too", glm::distance(rider.seen, {1, 2, 0}) < 1e-5f);
}

// A replayed tick re-runs what a tick computes, not what it presented: it plays no sound.
void testAReplayedTickPlaysNoSound() {
    std::printf("A sound played from a replayed tick:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    scene.add<Transform>(entity, Transform{});
    Chimer& chimer = attach<Chimer>(scene, entity);
    chimer.chime.name = "chime";

    ScriptFrame run(scene);
    run.frame.resources.add(AudioClipAsset{}, "chime");
    int      played = 0;
    EntityId from;
    run.frame.events.subscribe<PlaySoundEvent>([&](const PlaySoundEvent& sound) {
        ++played;
        from = sound.from;
    });

    run.fixed();
    run.frame.events.flush();
    check("a live tick plays it, from its entity", played == 1 && from == entity);

    run.frame.ownedNet.beginReplayTick(InputCommand{});
    run.fixed();
    run.frame.ownedNet.endReplay(scene);
    run.frame.events.flush();
    check("  and its replay does not again", played == 1);

    // A sound at a point need not be in this entity's body, so that body does not let it through.
    chimer.atPoint = true;
    run.fixed();
    run.frame.events.flush();
    check("one at a point sounds from no entity", played == 2 && !from);
}

void testASceneRequestIsDrainedEvenWhilePaused() {
    std::printf("Quitting to a menu with the world frozen:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Quitter& quitter = attach<Quitter>(scene, entity);

    ScriptFrame run(scene);
    // Started on a running frame, as nothing starts on the realtime pass.
    run.tick();

    // Now frozen. The realtime hook still runs and its request drains, ending the
    // session so the requester hears onDestroy - what makes a pause menu's "quit" work.
    run.pause(true);
    run.tick();
    check("a paused frame still drains the request", quitter.destroys == 1);
    check(
        "  and a load that cannot happen leaves the world alone",
        scene.isAlive(entity) && scene.entityCount() == 1
    );
}

// A restart starts over the scene the host began play with: the file it names, or, for a host whose
// scene is held in memory - the editor's - the host's own handler.
void testARestartStartsTheSceneBeingPlayedOver() {
    std::printf("A game that asks to start over:\n");

    const ScratchProject project("vkm_restart");
    std::error_code made;
    std::filesystem::create_directories(project.root() / "scenes", made);
    Scene authored;
    authored.createEntity();
    authored.createEntity();
    ResourceManager unused;
    const std::string path = (project.root() / "scenes" / "start.json").string();
    check("a scene file to begin from", SceneSerializer::save(authored, unused, path));

    Scene played;
    attach<Restarter>(played, played.createEntity());
    for (int i = 0; i < 3; ++i) played.createEntity();  // what play made since
    {
        ScriptFrame run(played);
        run.system.setPlayedScene("scenes/start.json");
        for (int i = 0; i < 4; ++i) run.tick();
    }
    check("the file the host began with is put back", played.entityCount() == 2);

    // A host that names it by its absolute path: the session is told it as the project names it,
    // for a server's players to be sent.
    Scene absolute;
    attach<Restarter>(absolute, absolute.createEntity());
    {
        ScriptFrame run(absolute);
        run.system.setPlayedScene(path);
        for (int i = 0; i < 4; ++i) run.tick();
        const bool named = run.frame.ctx.net.worldPath() == "scenes/start.json";
        check("  named to the session as the project names it", named);
    }

    // A load that fails leaves the scene being played as it was, and a restart starts that one over.
    BehaviorRegistry::get().registerBehaviors<Traveller>();
    Scene two;
    attach<Traveller>(two, two.createEntity()).step = 1;
    for (int i = 0; i < 4; ++i) two.createEntity();
    const std::string twoPath = (project.root() / "scenes" / "two.json").string();
    check("a second scene to go on to", SceneSerializer::save(two, unused, twoPath));
    Scene going;
    attach<Traveller>(going, going.createEntity());
    {
        ScriptFrame run(going);
        run.system.setPlayedScene("scenes/start.json");
        for (int i = 0; i < 6; ++i) run.tick();
    }
    check("after a load that failed, a restart starts over the one before it", going.entityCount() == 5);
    BehaviorRegistry::get().clear();

    Scene held;
    attach<Restarter>(held, held.createEntity());
    int asked = 0;
    {
        ScriptFrame run(held);
        run.system.setRestartHandler([&asked] { ++asked; });
        for (int i = 0; i < 4; ++i) run.tick();
    }
    check("a host holding its scene is asked to put it back itself", asked == 1);
    check("  and the world is left for it to replace", held.entityCount() == 1);
}

// A load holds its frame. With a loading screen, it waits until a frame of the cover has been
// shown, so the stall lands on the cover rather than on the world going; the cover then stays
// for the world it brings.
void testALoadWaitsBehindTheLoadingScreen() {
    std::printf("A load with a loading screen:\n");

    const ScratchProject project("vkm_load_behind");
    std::error_code made;
    std::filesystem::create_directories(project.root() / "scenes", made);
    Scene authored;
    authored.createEntity();
    authored.createEntity();
    ResourceManager unused;
    const std::string path = (project.root() / "scenes" / "start.json").string();
    check("a scene file to begin from", SceneSerializer::save(authored, unused, path));

    Scene played;
    attach<Restarter>(played, played.createEntity());
    for (int i = 0; i < 3; ++i) played.createEntity();
    ScriptFrame   run(played);
    LoadingSystem loading;
    loading.init(run.frame.ctx);
    run.system.setPlayedScene("scenes/start.json");
    // A frame as the engine runs one: last frame's events, then the screen, then the behaviors.
    const auto frame = [&] {
        run.frame.events.flush();
        loading.update(run.frame.ctx);
        run.tick();
    };

    frame();
    check("asked for, the world is not loaded at once", played.entityCount() == 4);
    frame();
    check(
        "  but while the cover is drawn",
        played.entityCount() == 4 && run.frame.ctx.loading->opacity == 1.0f
    );
    frame();
    check("once a covered frame was shown, it loads", played.entityCount() == 2);
    loading.update(run.frame.ctx);
    check(
        "  and the cover stays for the world it brought",
        loading.loading() && run.frame.ctx.loading->opacity == 1.0f
    );
}

// A client the server moves to another scene follows it there, though a behavior asks for one of
// its own while the load waits behind the loading screen.
void testTheServersSceneWinsWhileTheLoadWaits() {
    std::printf("A client the server moves on, behind a loading screen:\n");

    const ScopedEngineSchema wire;
    const ScratchProject     project("vkm_follow_server");
    std::error_code made;
    std::filesystem::create_directories(project.root() / "scenes", made);
    ResourceManager unused;
    const auto save = [&](const char* name, int entities) {
        Scene scene;
        for (int i = 0; i < entities; ++i) scene.createEntity();
        return SceneSerializer::save(scene, unused, (project.root() / "scenes" / name).string());
    };
    // Told apart by size: whatever the session spawns in either is a few entities more.
    check("two scene files", save("own.json", 2) && save("next.json", 40));

    ResourceManager serverAssets;
    Scene           serverWorld;
    NetSession      server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server binds a port", server.host(0, 2, TEST_TICK_RATE));

    Scene played;
    attach<Wanderer>(played, played.createEntity());
    ScriptFrame   run(played);
    LoadingSystem loading;
    loading.init(run.frame.ctx);
    NetSession& client = run.frame.ownedNet;
    client.setWorld(0, played);
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto net = [&] {
        server.receive(serverWorld, serverAssets);
        client.receive(played, run.frame.resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 1, TEST_ACTION_NAMES);
        client.beginTick(tick, InputCommand{}, 1, TEST_ACTION_NAMES);
        server.send(serverWorld, tick);
        client.send(played, tick);
        server.advance(0.016f);
        client.advance(0.016f);
    };
    check("the client joins", pumpUntil(net, [&] { return client.isPlaying(); }));

    // The server moves on as a load does: its world replaced, then named.
    Scene next;
    serverWorld.swap(next);
    server.setWorld(0, serverWorld, "scenes/next.json");
    for (int i = 0; i < 10; ++i) net();
    for (int i = 0; i < 8; ++i) {
        net();
        run.frame.events.flush();
        loading.update(run.frame.ctx);
        run.tick();
    }
    check("it loads the server's scene, not the one asked meanwhile", played.entityCount() >= 40);
}

void testAThrowingBehaviorIsDisabledNotFatal() {
    std::printf("A behavior that throws:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Thrower& thrower = attach<Thrower>(scene, entity);

    ScriptFrame run(scene);
    for (int i = 0; i < 3; ++i) {
        run.tick();
    }

    check("the throw is caught rather than leaving the frame", thrower.attempts >= 1);
    check("  and the behavior is not called again", thrower.attempts == 1);
    check("  while the entity is left standing", scene.isAlive(entity));
}

void testAThrowingListenerIsReportedNotFatal() {
    std::printf("A behavior whose event listener throws:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    EventThrower& thrower = attach<EventThrower>(scene, entity);

    ScriptFrame run(scene);
    run.tick();
    EventBus& events = run.frame.events;

    bool escaped = false;
    events.enqueue(ModuleEvent{1});
    try {
        events.flush();
    } catch (...) {
        escaped = true;
    }
    check("the throw is caught rather than leaving the flush", !escaped);
    check("  after the listener ran", thrower.attempts == 1);

    events.emit(ModuleEvent{2});
    check("  and the bus keeps delivering to it", thrower.attempts == 2);
}

void testEndSessionTearsDownWhatPlayStarted() {
    std::printf("What Stop does to a running behavior:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Recorder& recorder = attach<Recorder>(scene, entity);

    {
        ScriptFrame run(scene);
        run.tick();
        check("the session started it", recorder.starts == 1);

        run.system.endSession(scene);
        check("ending the session fires onDestroy", recorder.destroys == 1);

        // Started is reset, so the next Play runs the same instance's onStart again.
        run.tick();
        check("and the next session starts it afresh", recorder.starts == 2);
    }
}

// The queues are the system's, so ending a session empties them even with no
// behavior left; otherwise a last request would be served in the next world.
void testEndSessionEmptiesTheQueuesWithNoBehaviorLeft() {
    std::printf("What a session's last request leaves behind:\n");

    Scene scene;
    const EntityId target = scene.createEntity();
    const EntityId owner  = scene.createEntity();
    attach<Partner>(scene, owner).partner = target;

    ScriptFrame run(scene);
    run.tick();
    scene.destroyEntity(owner);
    run.system.endSession(scene);
    run.tick();
    check(
        "a request queued as the last behavior went is not served after the session",
        scene.isAlive(target)
    );
}

void testAModulesEventBusesGoWithIt() {
    std::printf("What a script reload leaves on the bus:\n");

    Scene scene;
    const EntityId entity = scene.createEntity();
    Listener& listener = attach<Listener>(scene, entity);

    ScriptFrame run(scene);
    EventBus& events = run.frame.events;

    // The engine's own listener, standing for those its systems keep across a reload.
    // Raw, because it is not a behavior's.
    int engineHeard = 0;
    events.subscribe<CollisionEvent>([&](const CollisionEvent&) { ++engineHeard; });

    run.tick();
    const std::size_t withModule = events.busCount();
    check("a game's own event type gets a bus of its own", withModule >= 2);

    events.emit(ModuleEvent{3});
    check("  and delivers on it while the module is loaded", listener.heard == 3);

    // ScriptModule::reload's order: behaviors' subscriptions first, then the buses
    // that leaves empty.
    run.system.endSession(scene);
    events.dropIdleBuses();

    check("a reload drops the bus nothing listens to any more", events.busCount() < withModule);

    // The bus whose vtable lives in the unmapped module is gone; the engine's, still
    // listened on, is not - or a reload would quietly stop every subscribed system.
    events.emit(CollisionEvent{});
    check("  and keeps the one the engine is still listening on", engineHeard == 1);
}

// Each contact phase reaches its own hook, on both entities. One destroyed while
// touching ends the contact for the survivor; the destroyed one, past onDestroy,
// hears nothing more.
void testContactPhasesReachTheirHooks() {
    std::printf("Contact hooks, and an entity destroyed while touching:\n");

    Scene scene;
    const EntityId floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId crate = addBox(scene, {0.0f, 0.49f, 0.0f}, {0.5f, 0.5f, 0.5f});
    scene.get<Rigidbody>(crate).motion   = RigidbodyMotion::Dynamic;
    scene.get<Rigidbody>(crate).canSleep = false;

    std::vector<Toucher::Heard> log;
    attach<Toucher>(scene, floor).log = &log;
    attach<Toucher>(scene, crate).log = &log;

    ScriptFrame run(scene);
    PhysicsSystem physics;
    // A tick, Engine::run's flush at the top of Simulation, and the update that
    // dispatches what it delivered.
    const auto frame = [&] {
        physics.fixedUpdate(run.frame.ctx);
        run.frame.events.flush();
        run.tick();
    };
    const auto heard = [&](const char* hook, EntityId entity, EntityId other) {
        int count = 0;
        for (const Toucher::Heard& h : log) {
            if (std::string(h.hook) == hook && h.entity == entity && h.other == other) ++count;
        }
        return count;
    };

    frame();
    check(
        "a contact beginning enters both",
        heard("enter", floor, crate) == 1 && heard("enter", crate, floor) == 1 && log.size() == 2
    );

    log.clear();
    frame();
    check(
        "  and lasting, stays on both",
        heard("stay", floor, crate) == 1 && heard("stay", crate, floor) == 1 && log.size() == 2
    );

    scene.destroyEntity(crate);
    log.clear();
    frame();
    check("destroyed while touching, the survivor hears it exit", heard("exit", floor, crate) == 1);
    check("  and the destroyed one hears nothing", log.size() == 1);
}

// A collision hook's normal points from the other entity into the receiver; the
// event's own normal is a -> b by slot, so each side gets a different flip of it.
void testACollisionNormalPointsIntoItsReceiver() {
    std::printf("A collision's normal, as each side hears it:\n");

    for (const bool crateFirst : {false, true}) {
        Scene scene;
        EntityId floor{};
        EntityId crate{};
        if (crateFirst) {
            crate = addBox(scene, {0.0f, 0.49f, 0.0f}, {0.5f, 0.5f, 0.5f});
            floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        } else {
            floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
            crate = addBox(scene, {0.0f, 0.49f, 0.0f}, {0.5f, 0.5f, 0.5f});
        }
        scene.get<Rigidbody>(crate).motion   = RigidbodyMotion::Dynamic;
        scene.get<Rigidbody>(crate).canSleep = false;

        std::vector<Toucher::Heard> log;
        attach<Toucher>(scene, floor).log = &log;
        attach<Toucher>(scene, crate).log = &log;

        ScriptFrame run(scene);
        PhysicsSystem physics;
        physics.fixedUpdate(run.frame.ctx);
        run.frame.events.flush();
        run.tick();

        float crateUp = 0.0f;
        float floorUp = 0.0f;
        for (const Toucher::Heard& h : log) {
            if (std::string(h.hook) != "enter") continue;
            if (h.entity == crate && h.other == floor) crateUp = h.normal.y;
            if (h.entity == floor && h.other == crate) floorUp = h.normal.y;
        }
        std::printf("  (the %s in the lower slot)\n", crateFirst ? "crate" : "floor");
        check("    the crate on the floor hears a normal pointing up, into itself", crateUp > 0.9f);
        check("    the floor under the crate hears one pointing down, into itself", floorUp < -0.9f);
    }
}

// A listener on the bus sees a contact as a hook does: the other side, and the normal into itself,
// whichever of the pair it is; an ended contact names only the other.
void testACollisionIsSeenFromEitherSide() {
    std::printf("A contact event, seen from each side:\n");

    Scene scene;
    const EntityId a = scene.createEntity();
    const EntityId b = scene.createEntity();
    CollisionEvent event;
    event.a      = a;
    event.b      = b;
    event.point  = {1.0f, 2.0f, 3.0f};
    event.normal = {0.0f, 1.0f, 0.0f};
    event.speed  = 4.0f;

    const Collision fromA = Collision::seenBy(event, a);
    const Collision fromB = Collision::seenBy(event, b);
    check("a sees b, the normal pointing into a", fromA.other == b && fromA.normal.y < -0.9f);
    check("  b sees a, the normal pointing into b", fromB.other == a && fromB.normal.y > 0.9f);
    check("  both at the point and speed", fromA.point == event.point && fromB.speed == 4.0f);
    event.phase = ContactPhase::Ended;
    const Collision ended = Collision::seenBy(event, a);
    check("  an ended one names only the other", ended.other == b && ended.speed == 0.0f);
}

void testCloneCarriesAuthoredFieldsOnly() {
    std::printf("What duplication copies:\n");

    Recorder original;
    original.speed   = 7.5f;
    original.updates = 42;

    const std::unique_ptr<Behavior> copy = original.clone();
    check(
        "clone answers the same type name",
        std::string(copy->typeName()) == std::string(Reflect::Traits<Recorder>::NAME)
    );

    const Recorder& copied = static_cast<const Recorder&>(*copy);
    check("an authored field comes across", nearly(copied.speed, 7.5f));
    // A duplicate is a fresh instance of the original's configuration, not its state.
    check("  and runtime state does not", copied.updates == 0);
}

void testRegistryKeysOffTheTypeName() {
    std::printf("What the registry recreates:\n");

    BehaviorRegistry& registry = BehaviorRegistry::get();
    registry.registerBehaviors<Recorder>();

    const std::unique_ptr<Behavior> made = registry.create(Reflect::Traits<Recorder>::NAME);
    check("a registered type is created by name", made != nullptr);
    check(
        "  and reports the name it was keyed by",
        made && std::string(made->typeName()) == std::string(Reflect::Traits<Recorder>::NAME)
    );
    check("an unregistered name creates nothing", registry.create("NoSuchBehaviorType") == nullptr);

    // Cleared before a module unload: its factories close over code about to leave.
    registry.clear();
    check("clear() drops the factories", registry.create(Reflect::Traits<Recorder>::NAME) == nullptr);
}

void testVectorFieldsRoundTrip() {
    std::printf("A behavior's vector fields through a save and a load:\n");

    BehaviorRegistry& registry = BehaviorRegistry::get();
    registry.registerBehaviors<Shaped>();

    ScriptComponent saved;
    auto made = std::make_unique<Shaped>();
    made->offset    = {3.0f, -4.0f};
    made->tintColor = {0.25f, 0.5f, 0.75f, 0.5f};
    made->facing    = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat facing = made->facing;
    saved.behaviors.push_back(std::move(made));

    ScriptComponent loaded;
    ComponentSerializer::load(ComponentSerializer::save(saved), loaded);
    const Behavior* first = loaded.behaviors.empty() ? nullptr : loaded.behaviors[0].get();
    const Shaped*   back  = static_cast<const Shaped*>(first);
    check("a glm::vec2 comes back", back && back->offset == glm::vec2(3.0f, -4.0f));
    check("  a glm::vec4", back && back->tintColor == glm::vec4(0.25f, 0.5f, 0.75f, 0.5f));
    check("  and a glm::quat", back && std::abs(glm::dot(back->facing, facing)) > 0.9999f);

    registry.clear();
}

// A prefab field saves as its path, a plain string a scene written by hand can hold, and loads
// back; it names no asset of the scene's.
void testAPrefabFieldIsItsPath() {
    std::printf("A behavior's prefab field:\n");

    BehaviorRegistry& registry = BehaviorRegistry::get();
    registry.registerBehaviors<Launcher>();

    ScriptComponent saved;
    auto made = std::make_unique<Launcher>();
    made->shell.path = "prefabs/shell.json";
    saved.behaviors.push_back(std::move(made));
    const nlohmann::json doc = ComponentSerializer::save(saved);

    ScriptComponent loaded;
    ComponentSerializer::load(doc, loaded);
    const Behavior* first = loaded.behaviors.empty() ? nullptr : loaded.behaviors[0].get();
    const Launcher* back  = static_cast<const Launcher*>(first);
    check("it comes back", back && back->shell.path == "prefabs/shell.json");
    check(
        "  saved as the plain path",
        doc["behaviors"][0]["properties"]["shell"] == nlohmann::json("prefabs/shell.json")
    );

    Scene scene;
    ResourceManager resources;
    const EntityId entity = scene.createEntity();
    attach<Launcher>(scene, entity).shell.path = "prefabs/shell.json";
    AssetSerializer::EntityAssetRefs refs;
    AssetSerializer::collectAssetRefs(scene, entity, resources, refs);
    check("  and names no asset of the scene's", refs.names.empty());

    registry.clear();
}

// A hand-edited scene mistypes values far more often than it breaks the file, so a
// mistyped field keeps its value and the rest loads.
void testAMistypedFieldCostsOneValueNotTheScene() {
    std::printf("A behavior field holding the wrong type:\n");

    BehaviorRegistry& registry = BehaviorRegistry::get();
    registry.registerBehaviors<Tuned>();

    const auto loaded = [](const nlohmann::json& properties, bool& threw) {
        const nlohmann::json behavior = {{"type", Reflect::Traits<Tuned>::NAME}, {"properties", properties}};
        const nlohmann::json doc = {{"behaviors", nlohmann::json::array({behavior})}};
        ScriptComponent sc;
        threw = false;
        try {
            ComponentSerializer::load(doc, sc);
        } catch (...) {
            threw = true;
        }
        return sc;
    };

    bool threw = false;
    const ScriptComponent wrong = loaded(
        {{"speed", "fast"}, {"lives", "many"}, {"armed", 2}, {"label", 7}},
        threw
    );
    check("the load goes on", !threw);
    check("  the behavior is built", wrong.behaviors.size() == 1);
    const Tuned* kept = wrong.behaviors.empty() ? nullptr
        : static_cast<const Tuned*>(wrong.behaviors[0].get());
    check(
        "  and every mistyped field keeps its value",
        kept && nearly(kept->speed, 1.0f) && kept->lives == 3 && kept->armed && kept->label == "start"
    );

    const ScriptComponent right = loaded(
        {{"speed", 2.5f}, {"lives", 5}, {"armed", false}, {"label", "set"}},
        threw
    );
    const Tuned* read = right.behaviors.empty() ? nullptr
        : static_cast<const Tuned*>(right.behaviors[0].get());
    check(
        "values of the right type still read",
        !threw
            && read
            && nearly(read->speed, 2.5f)
            && read->lives == 5
            && !read->armed
            && read->label == "set"
    );

    registry.clear();
}

// What an entity references is one walk; behaviors name assets in authored fields,
// which a walk over component handles alone would miss, calling such a clip unused.
void testABehaviorsAssetFieldIsAReference() {
    std::printf("What an entity references:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "test:cube");

    const EntityId entity = scene.createEntity();
    Mesh mesh;
    mesh.mesh = resources.findByName<MeshAsset>("test:cube");
    scene.add(entity, mesh);
    attach<Singer>(scene, entity).song.name = "sfx/hum.wav";

    AssetSerializer::EntityAssetRefs refs;
    AssetSerializer::collectAssetRefs(scene, entity, resources, refs);
    check("a component's handle is a reference", refs.handles.meshes.size() == 1);
    check(
        "  and so is the name a behavior authors",
        refs.names.size() == 1
            && refs.names[0].first == AssetType::AudioClip
            && refs.names[0].second == "sfx/hum.wav"
    );
}

// A material a behavior names brings its maps into the assets block, as one a Mesh holds does.
void testAMaterialABehaviorNamesBringsItsTextures() {
    std::printf("A material a behavior names:\n");

    Scene scene;
    ResourceManager resources;
    const TextureHandle picture = resources.add(TextureAsset{}, "test:scorch.png");
    MaterialAsset paint;
    paint.albedoTexture = picture;
    resources.add(std::move(paint), "test:scorch");

    const EntityId entity = scene.createEntity();
    attach<Painter>(scene, entity).paint.name = "test:scorch";

    const nlohmann::json assets = AssetSerializer::saveAssetsForEntities(scene, {entity}, resources);
    const auto listed = [&](const char* section, const char* name) {
        if (!assets.contains(section)) return false;
        for (const nlohmann::json& entry : assets[section]) {
            if (entry.value("name", std::string{}) == name) return true;
        }
        return false;
    };
    check("the material is listed", listed("materials", "test:scorch"));
    check("  and so is its albedo map", listed("textures", "test:scorch.png"));
}

// Whether DynamicLibrary::unload unmaps. Tracy's client makes dlclose a no-op so
// recorded zone names stay readable, so a profiler build keeps every copy mapped.
// These checks expect whichever holds; that the module CAN be unmapped is checked on
// Linux by testAReloadedModuleStartsItsStaticsOver, through libc's own dlclose.
#if VKM_PROFILER
constexpr bool UNLOAD_UNMAPS = false;
#else
constexpr bool UNLOAD_UNMAPS = true;
#endif

// Distinct mapped files whose name starts with @p prefix, from the kernel's list: a
// dlclose that does not unmap is invisible through the loader's API. -1 off Linux.
int mappedCopies(const std::string& prefix) {
#if defined(__linux__)
    std::ifstream maps("/proc/self/maps");
    if (!maps) return -1;
    std::set<std::string> copies;
    for (std::string line; std::getline(maps, line);) {
        const std::size_t at = line.find(prefix);
        if (at == std::string::npos) continue;
        copies.insert(line.substr(at, line.find(' ', at) - at));
    }
    return static_cast<int>(copies.size());
#else
    return -1;
#endif
}

// Copies of the test module ScriptModule has made that are still mapped.
int mappedModuleCopies() {
    return mappedCopies(
        std::filesystem::path(DynamicLibrary::platformName("test_module")).stem().string() + ".loaded."
    );
}

// A reload loads a fresh copy beside the old, and its statics must start over, as on
// Windows. GCC binds an inline static data member, or a static in an inline function,
// across every copy unless the module is built without unique symbols.
void testAReloadedModuleStartsItsStaticsOver() {
#ifndef VKM_TEST_MODULE_DIR
    std::printf("A static in a reloaded module (fixture not built - skipped):\n");
    return;
#else
    std::printf("A static in a reloaded module:\n");

    const std::filesystem::path built =
        std::filesystem::path(VKM_TEST_MODULE_DIR) / DynamicLibrary::platformName("test_module");
    std::error_code ec;
    if (!std::filesystem::exists(built, ec)) {
        std::printf("  (no module built on this platform - nothing to check)\n");
        return;
    }

    // Two names: loading one name twice returns the copy already open.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "vkm_reload_statics";
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path first  = dir / ("first" + built.extension().string());
    const std::filesystem::path second = dir / ("second" + built.extension().string());
    std::filesystem::copy_file(built, first, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::copy_file(built, second, std::filesystem::copy_options::overwrite_existing, ec);

    using CountFn = int (*)();   // vkmTestCountLoad, from script/test_module.h
    DynamicLibrary library;
    check("the first copy loads", library.load(first));
    auto count = reinterpret_cast<CountFn>(library.symbol("vkmTestCountLoad"));
    check("  and counts from one", count && count() == 1 && count() == 2);
    library.unload();

    check("the second copy loads", library.load(second));
    count = reinterpret_cast<CountFn>(library.symbol("vkmTestCountLoad"));
    check("  and counts from one again", count && count() == 1);
    library.unload();

#if defined(__linux__)
    // Whether a module CAN be unmapped, via libc's own dlclose: glibc never unmaps a
    // library holding a unique symbol, and a profiler build's dlclose is Tracy's no-op.
    const std::filesystem::path third = dir / ("third" + built.extension().string());
    std::filesystem::copy_file(built, third, std::filesystem::copy_options::overwrite_existing, ec);
    using DlcloseFn = int (*)(void*);
    void* libc = ::dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
    const auto realDlclose = libc ? reinterpret_cast<DlcloseFn>(::dlsym(libc, "dlclose")) : nullptr;
    void* handle = ::dlopen(third.c_str(), RTLD_NOW | RTLD_LOCAL);
    check("a third copy maps", handle && mappedCopies(third.filename().string()) == 1);
    if (handle && realDlclose) realDlclose(handle);
    check("  and libc's dlclose unmaps it", realDlclose && mappedCopies(third.filename().string()) == 0);
#endif

    std::filesystem::remove_all(dir, ec);
#endif
}

// ScriptModule needs a real shared library: tests/script/test_module.cpp, built beside
// the test binary. Covers the reload path, where a mistake is a crash: the module is
// unmapped while longer-lived registries may still point into it.
void testAModuleLoadsRegistersAndUnloadsCleanly() {
#ifndef VKM_TEST_MODULE_DIR
    std::printf("A gameplay module (fixture not built - skipped):\n");
    return;
#else
    std::printf("A gameplay module through load, reload and unload:\n");

    // Asked, not spelled: a hardcoded Linux name would skip this on other platforms.
    const std::filesystem::path built =
        std::filesystem::path(VKM_TEST_MODULE_DIR) / DynamicLibrary::platformName("test_module");
    std::error_code ec;
    if (!std::filesystem::exists(built, ec)) {
        std::printf("  (no module built on this platform - nothing to check)\n");
        return;
    }

    // Loaded from a copy this test can overwrite, as a build overwrites a module under
    // a running editor.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "vkm_reload_module";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path source = dir / built.filename();
    std::filesystem::copy_file(built, source, std::filesystem::copy_options::overwrite_existing, ec);

    // The module first, so it is destroyed last: behaviors' destructors and vtables
    // are module code, so a scene outliving the dlclose tears down through unmapped memory.
    ScriptModule module;
    Scene        scene;

    BehaviorRegistry::get().clear();
    check("nothing is registered before it loads", BehaviorRegistry::get().names().empty());

    // Null bus: what a host loading before it has an Engine passes; it has no module to
    // replace either.
    check("the module loads", module.load(source, nullptr));
    check("  and says so", module.isLoaded());
    const bool canSeeMappings = mappedModuleCopies() >= 0;
    if (canSeeMappings) check("  and is mapped, once", mappedModuleCopies() == 1);
    check("  having registered what it exports", BehaviorRegistry::get().create("Ticker") != nullptr);

    // The optional second entry: a world built in code, in a scene of its own, gone
    // before the reload. Component sets the module adds first are module code, and a
    // scene holding one across the swap tears down through an unmapped vtable (hidden
    // only by Linux's symbol interposition). A host serializes such a scene first, as
    // scripting.md says.
    ResourceManager resources;
    {
        Scene world;
        check("its build-scene entry is called", module.buildScene(world, resources));
        bool madeOne = false;
        world.forEach<Name>([&](EntityId, const Name& n) {
            if (std::string(n.value) == "Built By Module") madeOne = true;
        });
        check("  and the world it makes is in the scene", madeOne);
    }
    check(
        "  with the sound it made in the graph, which outlives the module's unload below",
        static_cast<bool>(resources.findByName<AudioClipAsset>("module:tone"))
    );

    ScriptComponent script;
    script.behaviors.push_back(BehaviorRegistry::get().create("Ticker"));
    const EntityId ticker = scene.createEntity();
    scene.add(ticker, makeName("Ticker"));
    scene.add(ticker, Transform{});
    scene.add(ticker, std::move(script));

    ScriptFrame run(scene);
    EventBus&   events = run.frame.events;
    const size_t busesBefore = events.busCount();

    // onStart subscribes to a module-declared event type, so the new Bus is module code
    // in a registry that outlives it - the case a reload must get right.
    run.tick();
    check("the module's behavior subscribed to its own event type", events.busCount() > busesBefore);

    check("it reloads", module.reload(scene, run.system, events));
    check("  and is still loaded", module.isLoaded());
    // Left mapped only where dlclose is Tracy's no-op; see UNLOAD_UNMAPS.
    if (canSeeMappings) {
        const char* what = UNLOAD_UNMAPS ? "  with the old copy unmapped" : "  with the old copy left mapped";
        check(what, mappedModuleCopies() == (UNLOAD_UNMAPS ? 1 : 2));
    }
    check("  with its behavior registered again", BehaviorRegistry::get().create("Ticker") != nullptr);
    check("  and the bus its old code owned went with it", events.busCount() == busesBefore);

    // Had an old-module bus survived, this flush would call into unmapped code;
    // returning is the check.
    events.flush();

    // A build that will not load - here not a library at all - is opened beside the
    // running module and refused before anything is torn down.
    run.tick();
    const Behavior* running = scene.get<ScriptComponent>(ticker).behaviors.front().get();
    std::ofstream(source, std::ios::binary | std::ios::trunc) << "not a library";
    check("a reload of a broken build is refused", !module.reload(scene, run.system, events));
    check("  leaving the running module loaded", module.isLoaded());
    check("  with its behavior types registered", BehaviorRegistry::get().contains("Ticker"));
    check(
        "  and the running behavior untouched",
        scene.get<ScriptComponent>(ticker).behaviors.size() == 1
            && scene.get<ScriptComponent>(ticker).behaviors.front().get() == running
    );
    if (canSeeMappings) {
        check("  having mapped nothing of the broken build", mappedModuleCopies() == (UNLOAD_UNMAPS ? 1 : 2));
    }
    std::filesystem::copy_file(built, source, std::filesystem::copy_options::overwrite_existing, ec);

    // The caller's job per unload(), done by the editor through beginSceneReplace:
    // behaviors are ended and their entities go while onDestroy's code is still mapped.
    run.system.endSession(scene);
    scene.clear();

    module.unload(events);
    check("it unloads", !module.isLoaded());
    if (canSeeMappings) {
        check(
            UNLOAD_UNMAPS ? "  and nothing of it is mapped" : "  and the profiler keeps both copies mapped",
            mappedModuleCopies() == (UNLOAD_UNMAPS ? 0 : 2)
        );
    }
    check("  taking its registrations with it", BehaviorRegistry::get().create("Ticker") == nullptr);

    // A project opened before its first build has no module, and must be told where one
    // will be, or a reload after the build has nothing to retry.
    module.expect(source, events);
    check(
        "a module expected rather than loaded knows where it will be",
        !module.isLoaded() && module.modulePath() == source
    );
    check(
        "  and a reload once it is built loads it",
        module.reload(scene, run.system, events) && module.isLoaded()
    );
    module.unload(events);
    std::filesystem::remove_all(dir, ec);
#endif
}

// A reload that finds no module loaded tries the load again; its failure is the Console's
// Errors' too, as the first load's was, so the editor's toast pointing there finds it.
void testAFailedRetryOfAModuleIsReported() {
    std::printf("A reload of a module that never loaded:\n");

    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "vkm_no_module"
        / DynamicLibrary::platformName("absent");
    ScriptModule module;
    Scene        scene;
    ScriptFrame  run(scene);

    EngineErrorLog errors;
    setErrorSink(&errors);
    const bool   loaded    = module.load(missing, nullptr);
    const size_t afterLoad = errors.entries().size();
    const bool   reloaded  = module.reload(scene, run.system, run.frame.events);
    setErrorSink(nullptr);

    check("neither the load nor the retry loads it", !loaded && !reloaded);
    check("  and each is reported", afterLoad == 1 && errors.entries().size() == 2);
}

} // namespace

void runScriptTests() {
    testAModuleLoadsRegistersAndUnloadsCleanly();
    testAFailedRetryOfAModuleIsReported();
    testAReloadedModuleStartsItsStaticsOver();
    testStartHappensOnSimulationTime();
    testEveryBehaviorStartsBeforeAnyHookRuns();
    testATierIsBuiltOverTheProjectsSettings();
    testPauseSplitsTheTwoTimelines();
    testFixedUpdateRunsOnTheFixedStep();
    testSpawnAndDeferredDestroy();
    testSpawnUnderAParent();
    testWhereABehaviorIsReadsThisTicksMoves();
    testAReplayedTickPlaysNoSound();
    testBehaviorsAreFoundAndAddedByType();
    testASceneRequestIsDrainedEvenWhilePaused();
    testARestartStartsTheSceneBeingPlayedOver();
    testALoadWaitsBehindTheLoadingScreen();
    testTheServersSceneWinsWhileTheLoadWaits();
    testAThrowingBehaviorIsDisabledNotFatal();
    testAThrowingListenerIsReportedNotFatal();
    testEndSessionTearsDownWhatPlayStarted();
    testEndSessionEmptiesTheQueuesWithNoBehaviorLeft();
    testAModulesEventBusesGoWithIt();
    testContactPhasesReachTheirHooks();
    testACollisionNormalPointsIntoItsReceiver();
    testACollisionIsSeenFromEitherSide();
    testCloneCarriesAuthoredFieldsOnly();
    testRegistryKeysOffTheTypeName();
    testAMistypedFieldCostsOneValueNotTheScene();
    testVectorFieldsRoundTrip();
    testAPrefabFieldIsItsPath();
    testABehaviorsAssetFieldIsAReference();
    testAMaterialABehaviorNamesBringsItsTextures();
}
