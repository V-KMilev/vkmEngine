// What a behavior asks of the world: the behaviors it finds, the entities and scenes it makes and ends,
// the events it hears, the questions a network game asks, and a move along where the player looks.

#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "base/math/axes.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"
#include "io/project/roots.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_serializer.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"
#include "net/match.h"

namespace ScriptVerbs {

using namespace Vkm::Engine;

/// What happened, in order: "<what> <slot>", and "frame <n>" at each frame's end.
std::vector<std::string>& journal() {
    static std::vector<std::string> s_lines;
    return s_lines;
}

void note(const std::string& what, EntityId id) { journal().push_back(
    what + " " + std::to_string(id.slot())
); }

/**
 * @brief Writes its start, ticks, frame passes and end into the journal.
 */
class VerbClerk : public Behavior<VerbClerk> {
    public:
        void onStart() override               { note("start", entity()); }
        void onFixedUpdate(float) override    { note("fixed", entity()); }
        void onUpdate(float) override         { note("update", entity()); }
        void onRealtimeUpdate(float) override { note("realtime", entity()); }
        void onDestroy() override             { note("destroy", entity()); }
};

/**
 * @brief Something the BeaconSeeker looks for.
 */
class SceneBeacon : public Behavior<SceneBeacon> {
    public:
        int number = 0;
};

/**
 * @brief Never carried by anything.
 */
class NeverCarried : public Behavior<NeverCarried> {};

/**
 * @brief Looks for the scene's Beacons as it starts.
 */
class BeaconSeeker : public Behavior<BeaconSeeker> {
    public:
        void onStart() override {
            if (const SceneBeacon* first = findInScene<SceneBeacon>()) firstFound = first->number;
            forEachBehavior<SceneBeacon>(
                [&](EntityId, SceneBeacon& beacon) { order.push_back(beacon.number); }
            );
            const SceneBeacon* onFar = findBehavior<SceneBeacon>(far);
            farNumber           = onFar ? onFar->number : -1;
            noneHere            = findBehavior<SceneBeacon>() == nullptr;
            selfFound           = findBehavior<BeaconSeeker>() == this;
            absentFound         = findInScene<NeverCarried>() != nullptr;
            gate                = findNamed("Gate");
        }

    public:
        EntityId         far;
        int              firstFound  = -1;
        int              farNumber   = -1;
        bool             noneHere    = false;
        bool             selfFound   = false;
        bool             absentFound = true;
        EntityId         gate;
        std::vector<int> order;
};

/**
 * @brief Destroys its own entity, and the part under it, on its second tick.
 */
class SelfDestroyer : public Behavior<SelfDestroyer> {
    public:
        void onFixedUpdate(float) override {
            if (++ticks == 2) destroy();
        }

    private:
        int ticks = 0;
};

/**
 * @brief Watches the SelfDestroyer one: whether it is alive, tick by tick.
 */
class DoomWitness : public Behavior<DoomWitness> {
    public:
        void onStart() override { watched = findNamed("SelfDestroyer"); }
        void onFixedUpdate(float) override { alive.push_back(scene().isAlive(watched)); }

    public:
        EntityId          watched;
        std::vector<bool> alive;
};

/**
 * @brief On its second tick makes a child of its own, a VerbClerk to run it, and a prefab where it was told.
 */
class SparkMaker : public Behavior<SparkMaker> {
    public:
        void onFixedUpdate(float) override {
            if (++ticks != 2) return;
            made = spawn("Spark", entity());
            addBehavior<VerbClerk>(scene(), made);
            Transform at;
            at.position = {4.0f, 5.0f, 6.0f};
            built       = instantiate(ember, at);
        }

    public:
        PrefabRef ember;
        EntityId  made;
        EntityId  built;

    private:
        int ticks = 0;
};

/**
 * @brief On its third tick sends the game to another scene.
 */
class SceneTraveler : public Behavior<SceneTraveler> {
    public:
        void onFixedUpdate(float) override {
            if (++ticks == 3) loadScene(next);
        }

    public:
        std::string next;

    private:
        int ticks = 0;
};

/**
 * @brief On its second tick, the first time, starts the scene over.
 */
class SceneRestarter : public Behavior<SceneRestarter> {
    public:
        void onFixedUpdate(float) override {
            if (++ticks == 2 && restarts++ == 0) restartScene();
        }

    public:
        static inline int restarts = 0;

    private:
        int ticks = 0;
};

/**
 * @brief Sent to Listeners.
 */
struct Ping {
    int value = 0;
};

/// Every Ping a PingListener heard, summed: kept outside them, so one heard after its behavior went is seen.
inline int g_heard = 0;

/**
 * @brief Listens for Pings from its start.
 */
class PingListener : public Behavior<PingListener> {
    public:
        void onStart() override {
            subscribe([](const Ping& ping) { g_heard += ping.value; });
        }
};

/**
 * @brief Asks the questions a networked behavior asks, on its ticks.
 */
class OfflineAsker : public Behavior<OfflineAsker> {
    public:
        void onFixedUpdate(float) override {
            simulated  = simulated && isSimulated();
            mine       = mine && isMine();
            authority  = authority && isAuthority();
            replaying  = replaying || isReplaying();
            jumped     = jumped || pressed("Jump");
            worldClock = worldTime();
        }

    public:
        bool   simulated  = true;
        bool   mine       = true;
        bool   authority  = true;
        bool   replaying  = false;
        bool   jumped     = false;
        double worldClock = 0.0;
};

/**
 * @brief Turns the axes into moves along the view, each tick.
 */
class ViewSteerer : public Behavior<ViewSteerer> {
    public:
        void onFixedUpdate(float) override {
            ahead  = alongView(0.0f, 1.0f);
            right  = alongView(1.0f, 0.0f);
            corner = alongView(1.0f, 1.0f);
            back   = alongView(0.0f, -0.5f);
        }

    public:
        glm::vec3 ahead{0.0f};
        glm::vec3 right{0.0f};
        glm::vec3 corner{0.0f};
        glm::vec3 back{0.0f};
};

/// Every call an after() made, by any TickTimer: kept outside them, so one made after its behavior went
/// is seen.
inline std::vector<int> g_called;

/**
 * @brief Asks, as it starts, for a call in half a second and for its target's end in as long.
 */
class TickTimer : public Behavior<TickTimer> {
    public:
        void onStart() override {
            after(0.5f, [this] { g_called.push_back(ticks); });
            if (target) destroyAfter(target, 0.5f);
        }
        void onFixedUpdate(float) override { ++ticks; }

    public:
        EntityId target;

    private:
        int ticks = 0;
};

/**
 * @brief On its player's Jump, asks for a shot and for its flare's end, each in half a second; counts the
 *        live ticks, which a replay does not run.
 */
class JumpTimer : public Behavior<JumpTimer> {
    public:
        void onFixedUpdate(float) override {
            if (!isReplaying()) {
                ++liveTicks;
                if (flare && diedAt < 0 && !scene().isAlive(flare)) diedAt = liveTicks;
            }
            if (!pressed("Jump")) return;
            if (isReplaying()) ++replayedPresses;
            else               pressedAt = liveTicks;
            after(0.5f, [this] { ++shots; });
            destroyAfter(flare, 0.5f);
        }

    public:
        EntityId flare;
        int      shots           = 0;
        int      liveTicks       = 0;
        int      pressedAt       = -1;
        int      diedAt          = -1;
        int      replayedPresses = 0;
};

} // namespace ScriptVerbs

VKM_BEHAVIOR(::ScriptVerbs::VerbClerk)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::SceneBeacon)
    VKM_F(number)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::NeverCarried)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::BeaconSeeker)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::SelfDestroyer)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::DoomWitness)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::SparkMaker)
    VKM_F(ember)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::SceneTraveler)
    VKM_F(next)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::SceneRestarter)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::PingListener)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::OfflineAsker)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::ViewSteerer)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::TickTimer)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptVerbs::JumpTimer)
VKM_REFLECT_END()

using namespace Vkm::Test;
using namespace Vkm::Platform;
using namespace ScriptVerbs;

namespace {

EntityId named(Scene& scene, const char* name) {
    const EntityId id = scene.createEntity();
    scene.add(id, makeName(name));
    scene.add(id, Transform{});
    return id;
}

/// Where in the journal @p line first is, at or after @p from; its size when nowhere.
size_t find(const std::string& line, size_t from = 0) {
    for (size_t i = from; i < journal().size(); ++i) {
        if (journal()[i] == line) return i;
    }
    return journal().size();
}

std::string at(const char* what, EntityId id) { return std::string(what) + " " + std::to_string(id.slot()); }

} // namespace

TEST("findInScene, forEachBehavior and findNamed answer lowest slot first, and findBehavior looks on one entity") {
    Game game;
    Scene& scene = game.scene();
    const EntityId seeker = scene.createEntity();
    const EntityId low    = scene.createEntity();
    const EntityId high   = scene.createEntity();
    // Given high slot first, so the storage holds them in the order slots do not.
    scene.add(high, makeName("Gate"));
    scene.add(low, makeName("Gate"));
    addBehavior<SceneBeacon>(scene, high).number = 2;
    addBehavior<SceneBeacon>(scene, low).number  = 1;
    BeaconSeeker& seeking = addBehavior<BeaconSeeker>(scene, seeker);
    seeking.far = high;

    game.run(1);

    CHECK(seeking.firstFound == 1);
    CHECK(seeking.order == std::vector<int>{1, 2});
    CHECK(seeking.farNumber == 2);
    CHECK(seeking.noneHere);
    CHECK(seeking.selfFound);
    CHECK(!seeking.absentFound);
    CHECK(seeking.gate == low);
}

TEST("destroy and spawn wait for the hook pass to end, and instantiate builds a prefab where it is told") {
    openProject();
    {
        Frame author;
        const EntityId ember = named(author.scene, "Ember");
        HierarchyOperations::setParent(author.scene, named(author.scene, "Ember glow"), ember);
        REQUIRE(Prefab::save(author.scene, ember, "prefabs/ember.json", author.resources));
    }
    Game game;
    Scene& scene = game.scene();
    const EntityId maker   = named(scene, "SparkMaker");
    const EntityId doomed  = named(scene, "SelfDestroyer");
    const EntityId part    = named(scene, "SelfDestroyer's part");
    const EntityId witness = named(scene, "DoomWitness");
    HierarchyOperations::setParent(scene, part, doomed);
    SparkMaker& making = addBehavior<SparkMaker>(scene, maker);
    making.ember = PrefabRef{"prefabs/ember.json"};
    addBehavior<SelfDestroyer>(scene, doomed);
    addBehavior<VerbClerk>(scene, doomed);
    const DoomWitness& watching = addBehavior<DoomWitness>(scene, witness);

    game.run(3, [&](int frame) { journal().push_back("frame " + std::to_string(frame)); });

    // Destroyed on tick 2 by its own hook: still there for the rest of that pass, gone with its part
    // after it, and told so once.
    CHECK(watching.alive == std::vector<bool>{true, true, false});
    CHECK(!scene.isAlive(doomed));
    CHECK(!scene.isAlive(part));
    CHECK(find(at("destroy", doomed)) < find("frame 2"));
    CHECK(find(at("destroy", doomed), find(at("destroy", doomed)) + 1) == journal().size());

    // Spawned on tick 2: there at once, named and under its maker; its behavior starts on the next
    // pass, the frame's, once that tick's pass and its destroys are done.
    REQUIRE(scene.isAlive(making.made));
    CHECK(std::string(scene.get<Name>(making.made).value) == "Spark");
    CHECK(scene.get<Hierarchy>(making.made).parent == maker);
    CHECK(find(at("start", making.made)) > find(at("destroy", doomed)));
    CHECK(find(at("start", making.made)) < find("frame 2"));

    REQUIRE(scene.isAlive(making.built));
    CHECK(std::string(scene.get<Name>(making.built).value) == "Ember");
    CHECK(scene.get<Transform>(making.built).position == glm::vec3(4.0f, 5.0f, 6.0f));
    int glows = 0;
    HierarchyOperations::forEachChild(scene, making.built, [&](EntityId child) {
        glows += std::string(scene.get<Name>(child).value) == "Ember glow";
    });
    CHECK(glows == 1);
}

TEST("loadScene replaces the world once the frame's hooks have run, and restartScene starts that scene over") {
    openProject();
    {
        Frame author;
        const EntityId next = named(author.scene, "Next");
        addBehavior<VerbClerk>(author.scene, next);
        addBehavior<SceneRestarter>(author.scene, next);
        REQUIRE(
            SceneSerializer::save(author.scene, author.resources, (Roots::scenes() / "next.json").string())
        );
    }
    Game game;
    Scene& scene = game.scene();
    const EntityId traveler = named(scene, "SceneTraveler");
    addBehavior<SceneTraveler>(scene, traveler).next = "scenes/next.json";
    const EntityId stayer = named(scene, "Stayer");
    addBehavior<VerbClerk>(scene, stayer);

    game.run(6, [&](int frame) { journal().push_back("frame " + std::to_string(frame)); });

    // Asked on tick 3, before the Stayer's tick: the Stayer still runs that tick and that frame's
    // passes, then ends with its world.
    const size_t frame2 = find("frame 2");
    const size_t frame3 = find("frame 3");
    CHECK(find(at("fixed", stayer), frame2) < frame3);
    CHECK(find(at("update", stayer), frame2) < frame3);
    CHECK(find(at("realtime", stayer), frame2) < frame3);
    CHECK(find(at("destroy", stayer), frame2) < frame3);

    // The new world runs from the next frame; its SceneRestarter starts it over on its second tick, once.
    const EntityId next = findLowestSlot<Name>(
        scene,
        [](const Name& name) { return std::string(name.value) == "Next"; }
    );
    REQUIRE(next);
    CHECK(
        !findLowestSlot<Name>(
            scene,
            [](const Name& name) { return std::string(name.value) == "SceneTraveler"; }
        )
    );
    const size_t started   = find(at("start", next), frame3);
    const size_t restarted = find(at("start", next), started + 1);
    CHECK(started < find("frame 4"));
    CHECK(find(at("destroy", next), started) < find("frame 5"));
    CHECK(restarted > find("frame 5"));
    CHECK(restarted < find("frame 6"));
    CHECK(SceneRestarter::restarts == 1);
}

TEST("after and destroyAfter wait out their time in ticks, and a call goes with its behavior") {
    Game game;
    Scene& scene = game.scene();
    const EntityId doomed = named(scene, "Doomed later");
    TickTimer& timing = addBehavior<TickTimer>(scene, named(scene, "Timer"));
    timing.target = doomed;
    const EntityId brief = named(scene, "Brief");
    addBehavior<TickTimer>(scene, brief);

    int doomedAt = 0;
    game.run(90, [&](int frame) {
        if (frame == 10) scene.destroyEntity(brief);
        if (doomedAt == 0 && !scene.isAlive(doomed)) doomedAt = frame;
    });

    // Half a second at 60 ticks a second; the brief one's call never came.
    CHECK(g_called == std::vector<int>{30});
    CHECK(doomedAt == 30);
}

TEST("a replayed tick asks no after() and restarts no destroyAfter: each is asked once, by the live tick") {
    Match match;
    Scene& client    = match.client.scene();
    JumpTimer& timer = addBehavior<JumpTimer>(client, match.walkers[0]);
    match.play(150, [&](int frame) {
        if (frame == 40) timer.flare = client.createEntity();
        // The server moves the character on its own, so each snapshot corrects the client, which replays
        // the ticks since, the press among them.
        Scene& server = match.server.scene();
        if (frame >= 70 && frame < 110) server.get<Transform>(match.walkers[0]).position.z += 0.02f;
        if (frame == 80) match.hold(Key::Space);
        if (frame == 81) match.hold(Key::Space, false);
    });

    REQUIRE(timer.pressedAt > 0);
    REQUIRE(timer.replayedPresses > 0);
    CHECK(timer.shots == 1);
    CHECK(timer.diedAt - timer.pressedAt == 30);
}

TEST("subscribe hears the event its lambda takes, until its behavior goes") {
    Game game;
    Scene& scene = game.scene();
    const EntityId ear = named(scene, "Ear");
    addBehavior<PingListener>(scene, ear);

    int beforeEnd = 0;
    game.run(5, [&](int frame) {
        if (frame == 2) game.events().emit(Ping{5});
        if (frame == 3) scene.destroyEntity(ear);
        if (frame == 4) {
            game.events().emit(Ping{100});
            beforeEnd = g_heard;
        }
    });

    CHECK(beforeEnd == 5);
}

TEST("offline, every question a networked behavior asks answers yes, and the tick's command is the player's") {
    Game game;
    game.engine.getInput().setActions({{"Jump", {InputBinding::key(Key::Space)}}});
    OfflineAsker& asking = addBehavior<OfflineAsker>(game.scene(), named(game.scene(), "OfflineAsker"));

    game.run(10, [&](int frame) {
        game.engine.getWindow().getInputHandle().onKeyEvent(Key::Space, frame == 4);
    });

    CHECK(asking.simulated);
    CHECK(asking.mine);
    CHECK(asking.authority);
    CHECK(!asking.replaying);
    CHECK(asking.jumped);
    // The authority's world clock is this end's own: ten ticks.
    CHECK_NEAR(asking.worldClock, 10.0 / 60.0, 1e-6);
}

TEST("alongView turns an axis pair into a level move along where the camera looks, never longer than one") {
    for (const float pitch : {-40.0f, -90.0f}) {
        Game game;
        Scene& scene = game.scene();
        const EntityId eye = named(scene, "Eye");
        scene.add(eye, Camera{});
        // Turned a quarter left, then tipped down.
        const glm::quat yaw  = glm::angleAxis(glm::radians(90.0f), Math::WORLD_UP);
        const glm::quat tilt = glm::angleAxis(glm::radians(pitch), Math::WORLD_RIGHT);
        scene.get<Transform>(eye).rotation = yaw * tilt;
        ViewSteerer& steering = addBehavior<ViewSteerer>(scene, named(scene, "Walker"));

        game.run(3);

        // Forward is -Z and right +X, so a quarter turn left faces -X with its right at -Z. Looking
        // straight down, the top of the screen is ahead.
        CHECK_NEAR(steering.ahead, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-5f);
        CHECK_NEAR(steering.right, glm::vec3(0.0f, 0.0f, -1.0f), 1e-5f);
        CHECK_NEAR(steering.corner, glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f)), 1e-5f);
        CHECK_NEAR(steering.back, glm::vec3(0.5f, 0.0f, 0.0f), 1e-5f);
    }
}
