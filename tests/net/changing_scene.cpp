// A server that moves its session to another scene, as a behavior's loadScene does, with a client in
// lockstep: the player goes along, and nothing made for the old world reaches the new one.

#include <string>

#include "core/project_boot.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"
#include "net/net_session.h"
#include "net/wire/codecs.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/lockstep.h"
#include "harness/world.h"

namespace SceneChange {

using namespace Vkm::Engine;

/**
 * @brief A player's character: walks along X as its player holds Right, where this end simulates it.
 */
class SceneStepper : public Behavior<SceneStepper> {
    public:
        void onFixedUpdate(float dt) override {
            if (isSimulated()) get<Transform>().position.x += 4.0f * axis("Right") * dt;
        }
};

/**
 * @brief On the authority, on its tick @c atTick, sends the session to @c next.
 */
class SceneDirector : public Behavior<SceneDirector> {
    public:
        void onFixedUpdate(float) override {
            if (isAuthority() && ++ticks == atTick) loadScene(next);
        }

    public:
        std::string next;
        int         atTick = 0;

    private:
        int ticks = 0;
};

} // namespace SceneChange

VKM_BEHAVIOR(::SceneChange::SceneStepper)
VKM_REFLECT_END()

VKM_BEHAVIOR(::SceneChange::SceneDirector)
    VKM_F(next)
    VKM_F(atTick)
VKM_REFLECT_END()

using namespace Vkm::Test;
using namespace Vkm::Platform;
using namespace SceneChange;

namespace {

EntityId place(Scene& scene, const char* name, const glm::vec3& at) {
    const EntityId id = scene.createEntity();
    scene.add(id, makeName(name));
    Transform transform;
    transform.position = at;
    scene.add(id, transform);
    return id;
}

/// The lowest-slot entity of @p scene named @p name, and how many are.
EntityId named(const Scene& scene, const char* name, int* count = nullptr) {
    EntityId found;
    int seen = 0;
    scene.forEach<Name>([&](EntityId id, const Name& each) {
        if (std::string(each.value) != name) return;
        ++seen;
        if (!found || id.slot() < found.slot()) found = id;
    });
    if (count) *count = seen;
    return found;
}

float xOf(const Scene& scene, EntityId id) { return scene.get<Transform>(id).position.x; }

/// Two scenes, each with a character for the player: the first sends the session to the second on
/// its 60th tick.
void writeScenes() {
    {
        Frame author;
        SceneDirector& director = addBehavior<SceneDirector>(
            author.scene,
            place(author.scene, "SceneDirector", glm::vec3(0.0f))
        );
        director.next      = "scenes/second.json";
        director.atTick    = 60;
        addBehavior<SceneStepper>(author.scene, place(author.scene, "Player", {10.0f, 0.0f, 0.0f}));
        place(author.scene, "Old crate", {5.0f, 0.0f, 0.0f});
        REQUIRE(
            SceneSerializer::save(author.scene, author.resources, (Roots::scenes() / "first.json").string())
        );
    }
    Frame author;
    place(author.scene, "New crate", {-5.0f, 0.0f, 3.0f});
    place(author.scene, "New crate", {-5.0f, 0.0f, -3.0f});
    addBehavior<SceneStepper>(author.scene, place(author.scene, "Player", {-10.0f, 0.0f, 0.0f}));
    REQUIRE(
        SceneSerializer::save(author.scene, author.resources, (Roots::scenes() / "second.json").string())
    );
}

/// Stand @p game up on the project's world, as a host does.
void boot(Game& game, const Project& project) {
    REQUIRE(bootProjectWorld(
        project,
        game.scene(),
        game.resources(),
        game.clock(),
        game.engine.getInput(),
        game.engine.getNet()
    ).source == SceneBoot::Project);
}

} // namespace

TEST("a server that changes scene takes its player along, and runs no command made for the world before") {
    openProject();
    writeScenes();
    writeText(
        Roots::projectRoot() / "project.json",
        R"({"name": "travel", "entryScene": "scenes/first.json", "tickRate": 60, "actions": {"Right": ["D"]}})"
    );
    Project project;
    loadProject(Roots::projectRoot(), project);
    registerEngineNetTypes();

    Game server;
    Game client;
    boot(server, project);
    boot(client, project);
    NetSession& serverNet = server.engine.getNet();
    NetSession& clientNet = client.engine.getNet();
    // A player takes the scene's character.
    serverNet.onSpawn(
        [&serverNet](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId character = named(scene, "Player");
            return serverNet.isDriven(character) ? EntityId{} : character;
        },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    REQUIRE(server.host(0, 4, project.tickRate));
    REQUIRE(client.join(loopback(serverNet.localAddress().port), project.tickRate));

    // Right held through the first world, let go the moment the server is in the second: what the
    // first world's commands say must not move the second world's character.
    client.engine.getWindow().getInputHandle().onKeyEvent(Key::D, true);
    constexpr int FRAMES = 180;
    float walked = 0.0f;
    int movedAt = 0;
    PlayerId before = 0;
    // The ends as they stand on the last frame, before each closes its session.
    struct End {
        bool     playing = false;
        size_t   players = 0;
        PlayerId player  = 0;
        EntityId entity;
        bool     driven  = false;
        float    x       = 0.0f;
    } atServer, atClient;
    runLockstep(server, client, FRAMES, [&](int frame) {
        if (frame == 59) {
            walked = xOf(server.scene(), named(server.scene(), "Player"));
            before = clientNet.localPlayer();
        }
        if (movedAt == 0 && named(server.scene(), "New crate")) {
            movedAt = frame;
            client.engine.getWindow().getInputHandle().onKeyEvent(Key::D, false);
        }
        if (frame != FRAMES) return;
        const EntityId theirs = named(server.scene(), "Player");
        atServer              = {true, serverNet.playerCount(), 0, theirs, serverNet.isDriven(theirs),
                                 xOf(server.scene(), theirs)};
        const EntityId mine = clientNet.localEntity();
        atClient              = {clientNet.isPlaying(), 0, clientNet.localPlayer(), mine, false,
                                 client.scene().isAlive(mine) ? xOf(client.scene(), mine) : 0.0f};
    });

    REQUIRE(movedAt > 0);
    CHECK(walked > 11.0f);

    // The server and its player both play on in the second world, the player's number kept.
    int newCrates = 0;
    named(client.scene(), "New crate", &newCrates);
    CHECK(newCrates == 2);
    CHECK(!named(client.scene(), "Old crate"));
    CHECK(atClient.playing);
    CHECK(atServer.players == 1u);
    CHECK(atClient.player == before);
    REQUIRE(atClient.entity);
    CHECK(atClient.entity == named(client.scene(), "Player"));
    CHECK(atServer.driven);

    // Nothing has moved it since: Right was let go before any command for this world was made.
    CHECK_NEAR(atServer.x, -10.0f, 1e-4f);
    CHECK_NEAR(atClient.x, -10.0f, 1e-4f);
}
