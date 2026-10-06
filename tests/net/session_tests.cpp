#include "physics/physics_support.h"
#include "net/net_support.h"

#include <set>

#include "project_boot.h"
#include "core/math/random.h"
#include "system/physics/character/character_controller_system.h"
#include "system/physics/physics_system.h"

namespace {

void testTwoSessionsPlayTheSameGame() {
    std::printf("A server and a client, on real sockets, joining and playing:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 30);

    EntityId playerOnServer;
    NetSession server;
    server.onSpawn(
        [&playerOnServer](Scene& scene, ResourceManager&, PlayerId id) {
            const EntityId player = scene.createEntity();
            Transform at;
            at.position = {static_cast<float>(id) * 3.0f, 1.0f, -5.0f};
            scene.add(player, at);
            scene.add(player, Rigidbody{});
            playerOnServer = player;
            return player;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId entity) { scene.destroyEntity(entity); }
    );

    check("the server binds a port", server.host(0, 4, TEST_TICK_RATE));
    check("and is refereeing straight away", server.isPlaying());
    check("a host drives nobody, so every player is a real client", !server.localEntity());

    Scene clientWorld;
    NetSession client;
    check(
        "the client opens a socket",
        client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE)
    );
    check("but is not playing until it is welcomed", !client.isPlaying());

    // Three actions; both ends agree because both run the same project.
    constexpr uint32_t ACTIONS = 3;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, ACTIONS);
        client.beginTick(tick, InputCommand{}, ACTIONS);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(0.016f);
        client.advance(0.016f);
    };

    check("the client is welcomed", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and given a player number", client.localPlayer() != NO_PLAYER);
    check("and an entity to drive", clientWorld.isAlive(client.localEntity()));

    check("the server counts the one player in it", server.playerCount() == 1);
    check("and still owns nothing itself", !server.localEntity());

    check("the world arrives", pumpUntil(frame, [&]() { return clientWorld.entityCount() >= 31; }, 80));
    std::printf(
        "      %zu entities on the server, %zu on the client\n",
        serverWorld.entityCount(),
        clientWorld.entityCount()
    );

    // The crates the server built are where the server has them.
    const EntityId probe = crates[17];
    const EntityId mirror = clientWorld.entityAt(probe.slot());
    check("a crate the client was never told about directly is there", clientWorld.isAlive(mirror));
    check(
        "at the position the server has it",
        std::abs(clientWorld.get<Transform>(mirror).position.x - serverWorld.get<Transform>(probe).position.x)
            < 0.002f
    );

    // The server moves something; it must reach the client unasked.
    serverWorld.get<Transform>(probe).position = {-77.5f, 12.25f, 3.0f};
    const auto mirrored = [&]() {
        return std::abs(clientWorld.get<Transform>(mirror).position.x + 77.5f) < 0.002f;
    };
    check("a change on the server reaches the client", pumpUntil(frame, mirrored));

    // The client predicts its own entity only, or it would shove every crate its way
    // and be corrected.
    check("the client simulates what it owns", client.simulates(client.localEntity()));
    check("and not the crates", !client.simulates(mirror));
    check("and knows which is its own", client.isMine(client.localEntity()));
    check("the server simulates everything", server.simulates(probe));
    check("but owns nothing itself", !server.isMine(probe));

    // Input goes the other way, and the server runs it on the tick it names.
    const uint32_t inputTick = tick + 1;
    InputCommand walking;
    walking.sequence = 500;
    walking.tick     = inputTick;
    walking.axis[0]  = 1.0f;    // move_forward, held
    walking.pressed  = 1u << 2; // jump, this tick only
    client.beginTick(inputTick, walking, ACTIONS);
    client.send(clientWorld, inputTick);
    server.receive(serverWorld, resources);
    server.beginTick(inputTick, InputCommand{}, ACTIONS);

    check("the server spawned the player it is refereeing", bool(playerOnServer));
    const InputCommand& ran = server.commandFor(playerOnServer);
    check("the server runs the client's input on the client's entity", std::abs(ran.axis[0] - 1.0f) < 0.01f);
    check("with the edge intact", ran.pressed == (1u << 2));
    check("and nothing for an entity no player drives", server.commandFor(probe).pressed == 0);

    // Half a second, well inside NetConnection::TIMEOUT_SECONDS: past it, a silent client
    // would free the seat too, and the check would prove nothing about the Goodbye.
    client.close();
    server.receive(serverWorld, resources);
    server.advance(0.5f);
    server.receive(serverWorld, resources);
    check("the seat is freed by the message, long before any timeout", server.playerCount() == 0);

    server.close();
}

void testAClientBuiltFromDifferentSourceIsTurnedAway() {
    std::printf("What happens when two ends do not agree on what a world is:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server is up", server.host(0, 4, TEST_TICK_RATE));

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    // The hello carries a fingerprint of what this build replicates and is sent before
    // the list changes, so the server reads what is, in effect, another build.
    client.beginTick(1, InputCommand{}, 3);
    client.send(clientWorld, 1);

    NetSchema::get().replicate<Transform>("Health");

    bool refused = false;
    for (int i = 0; i < 20 && !refused; ++i) {
        server.receive(serverWorld, resources);
        server.send(serverWorld, 1);
        client.receive(clientWorld, resources);
        refused = client.isDisconnected() && !client.lastError().empty();
    }
    check("the client is refused rather than left to decode nonsense", refused);
    check("and told why", client.lastError() == std::string("a different build or world"));
    check("no seat was taken", server.playerCount() == 0);

    server.close();
}

// Actions travel by slot, in the order a project defined them; the same three in
// another order agree on the count and disagree on every bit.
void testTheSameActionsInAnotherOrderAreTurnedAway() {
    std::printf("Two ends with the same actions, defined in a different order:\n");

    InputMap serverActions, clientActions, sameActions;
    for (const char* name : {"Move", "Jump", "Fire"}) serverActions.define(name, {});
    for (const char* name : {"Fire", "Jump", "Move"}) clientActions.define(name, {});
    for (const char* name : {"Move", "Jump", "Fire"}) sameActions.define(name, {});
    check("both define three", serverActions.actionCount() == clientActions.actionCount());
    check(
        "  and the fingerprint tells the orders apart",
        serverActions.actionFingerprint() != clientActions.actionFingerprint()
    );
    check(
        "  while one order is one fingerprint",
        serverActions.actionFingerprint() == sameActions.actionFingerprint()
    );

    const ScopedEngineSchema wire;
    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server is up", server.host(0, 4, TEST_TICK_RATE));

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3, serverActions.actionFingerprint());
        client.beginTick(tick, InputCommand{}, 3, clientActions.actionFingerprint());
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check(
        "the client is refused rather than seated",
        pumpUntil(frame, [&]() { return client.isDisconnected(); })
    );
    check("  as a different build", client.lastError() == std::string("a different build or world"));
    check("  and no seat was taken", server.playerCount() == 0);

    server.close();
}

// The tick rate is the project's (project.json), and nothing else in the Hello covers
// it: one build and scene can be served at 128 and joined at 64, every command running
// twice as long on one end. So the join is refused, with a reason a player can read.
void testAClientAtAnotherTickRateIsTurnedAway() {
    std::printf("A client ticking at a different rate from its server:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server is up", server.host(0, 4, TEST_TICK_RATE));

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE * 2);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check(
        "the client is refused rather than seated",
        pumpUntil(frame, [&]() { return client.isDisconnected(); })
    );
    check("  saying it is the tick rate", client.lastError() == std::string(toString(NetRefusal::TickRate)));
    check("  and no seat was taken", server.playerCount() == 0);

    server.close();
}

/**
 * @brief A Hello or a Refuse as another build writes one: this layout, the next
 *        protocol version.
 *
 * @param message Hello or Refuse.
 * @param token   The connection token the message carries.
 * @param reason  The refusal, written only for a Refuse.
 * @return The framed datagram.
 */
std::vector<uint8_t> fromAnotherBuild(NetMessage message, uint64_t token, NetRefusal reason) {
    std::vector<uint8_t> payload;
    BitWriter writer(payload, 32);
    writer.u16(NET_PROTOCOL_TAG);
    writer.u8(static_cast<uint8_t>(NET_PROTOCOL_VERSION + 1));
    writer.u8(static_cast<uint8_t>(message));
    writer.u64(token);
    if (message == NetMessage::Refuse) writer.u8(static_cast<uint8_t>(reason));
    writer.finish();

    NetConnection connection;
    std::vector<uint8_t> datagram;
    connection.frame(payload.data(), payload.size(), datagram);
    return datagram;
}

// Builds on different wire versions cannot read each other's handshake, and an
// unanswered Hello looks exactly like a firewall. Every message's first bytes are the
// same in every version, so each end can still say what is wrong.
void testAnotherBuildIsToldRatherThanIgnored() {
    std::printf("Two builds speaking different versions of the wire:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server is up", server.host(0, 4, TEST_TICK_RATE));

    UdpSocket other;
    check("another build's socket opens", other.open(0));
    const NetAddress serverAddress{0x7F000001u, server.localAddress().port};
    const std::vector<uint8_t> hello = fromAnotherBuild(NetMessage::Hello, 0, NetRefusal::Count);
    other.send(serverAddress, hello.data(), hello.size());

    std::vector<uint8_t> answer;
    NetAddress from;
    bool answered = false;
    for (int i = 0; i < 20 && !answered; ++i) {
        server.receive(serverWorld, resources);
        answered = other.receive(answer, from);
    }
    const size_t at = NetConnection::HEADER_BYTES;
    check("a Hello from another build is answered", answered && answer.size() > at + 12);
    check(
        "  with a refusal saying the two are different builds",
        answered
            && answer.size() > at + 12
            && answer[at + 3] == static_cast<uint8_t>(NetMessage::Refuse)
            && answer[at + 12] == static_cast<uint8_t>(NetRefusal::Mismatch)
    );
    check("  and no seat is taken", server.playerCount() == 0);
    server.close();

    // The other way: a client joining a server of another build.
    UdpSocket otherServer;
    check("another build's server opens", otherServer.open(0));
    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, otherServer.localAddress().port}, TEST_TICK_RATE);
    client.beginTick(1, InputCommand{}, 3);
    client.send(clientWorld, 1);

    NetAddress joiner;
    bool heard = false;
    for (int i = 0; i < 20 && !heard; ++i) heard = otherServer.receive(answer, joiner);
    check("the client says hello", heard);
    const std::vector<uint8_t> refusal =
        fromAnotherBuild(NetMessage::Refuse, 0, NetRefusal::Mismatch);
    otherServer.send(joiner, refusal.data(), refusal.size());

    pumpUntil([&]() { client.receive(clientWorld, resources); }, [&]() { return client.isDisconnected(); });
    check("a refusal from another build reaches the client", client.isDisconnected());
    check(
        "  which says it is a different build",
        client.lastError() == std::string(toString(NetRefusal::Mismatch))
    );
}

// Scale travels as raw floats. Clamped to NET_MAX_SCALE by the reader alone, a body
// authored at 2000 would be 2000 on the server and 1024 on clients, for good. Both
// ends hold the wire's limit - except an undescribed body, which is the scene file's.
void testBothEndsHoldTheScaleTheWireCarries() {
    std::printf("A body scaled past what the wire carries:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    EntityId huge, poisoned, wall;
    const auto build = [&](Scene& scene) {
        Transform big;
        big.scale = {2000.0f, 1.0f, -3000.0f};
        huge = scene.createEntity();
        scene.add(huge, big);

        Transform nan;
        nan.scale = {std::numeric_limits<float>::quiet_NaN(), 2.0f, 2.0f};
        poisoned = scene.createEntity();
        scene.add(poisoned, nan);

        wall = scene.createEntity();
        scene.add(wall, big);
        Rigidbody fixed;
        fixed.motion = RigidbodyMotion::Static;
        scene.add(wall, fixed);
    };

    Scene serverWorld, clientWorld;
    build(serverWorld);
    build(clientWorld);

    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server is up", server.host(0, 4, TEST_TICK_RATE));
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    for (int i = 0; i < 20; ++i) frame();

    const glm::vec3 served = serverWorld.get<Transform>(huge).scale;
    const glm::vec3 shown  = clientWorld.get<Transform>(huge).scale;
    check("a scale past the bound is the same at both ends, bit for bit", served == shown);
    check("  the bound itself", served == glm::vec3(NET_MAX_SCALE, 1.0f, -NET_MAX_SCALE));
    check(
        "a scale that is not a number is one at both ends",
        serverWorld.get<Transform>(poisoned).scale == glm::vec3(1.0f)
            && clientWorld.get<Transform>(poisoned).scale == glm::vec3(1.0f)
    );
    check(
        "a static body, never described, keeps what both ends loaded",
        serverWorld.get<Transform>(wall).scale.x == 2000.0f
            && clientWorld.get<Transform>(wall).scale.x == 2000.0f
    );

    client.close();
    server.close();
}

// Both ends compare a fingerprint of their scene file; a Windows checkout may have
// CRLF line ends, and the same world must still read as the same.
void testOneWorldHasOneFingerprintOnEitherPlatform() {
    std::printf("The same scene, checked out with either line ending:\n");

    const std::filesystem::path lfPath =
        runScratch() / "vkm_world_lf.json";
    const std::filesystem::path crlfPath =
        runScratch() / "vkm_world_crlf.json";
    {
        std::ofstream lf(lfPath, std::ios::binary);
        lf << "{\n  \"entities\": []\n}\n";
        std::ofstream crlf(crlfPath, std::ios::binary);
        crlf << "{\r\n  \"entities\": []\r\n}\r\n";
    }

    const uint64_t onLinux   = fingerprintScene(lfPath);
    const uint64_t onWindows = fingerprintScene(crlfPath);
    check("a scene file has a fingerprint", onLinux != 0);
    check("  and it does not depend on how the checkout ended its lines", onLinux == onWindows);

    std::error_code ec;
    std::filesystem::remove(lfPath, ec);
    std::filesystem::remove(crlfPath, ec);
}

void testAFullServerSaysSoRatherThanIgnoring() {
    std::printf("A server with one seat, and two players who want it:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 1, TEST_TICK_RATE);
    const uint16_t port = server.localAddress().port;

    Scene firstWorld, secondWorld;
    NetSession first, second;
    first.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto pump = [&]() {
        server.receive(serverWorld, resources);
        first.receive(firstWorld, resources);
        second.receive(secondWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        first.beginTick(tick, InputCommand{}, 3);
        second.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        first.send(firstWorld, tick);
        second.send(secondWorld, tick);
    };

    check("the first player gets the seat", pumpUntil(pump, [&]() { return first.isPlaying(); }));

    second.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);
    bool told = false;
    for (int i = 0; i < 20 && !told; ++i) {
        pump();
        told = second.isDisconnected() && !second.lastError().empty();
    }
    check("the second is told the game is full", told);
    check("in words a player can be shown", second.lastError() == std::string("the server is full"));

    // Refused is not Offline: an offline end decides everything, so a turned-away client
    // would silently play the scene alone.
    check(
        "and is left deciding nothing rather than playing alone",
        !second.isOffline() && !second.simulates(secondWorld.createEntity())
    );
    check("and the first player is undisturbed", first.isPlaying());

    first.close();
    server.close();
}

// Games key per-player state on the player number, so one spent on a declined join
// is a gap in every table.
void testADeclinedJoinSpendsNoPlayerNumber() {
    std::printf("A join the game declines, and the one after it:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    int asked = 0;
    NetSession server;
    server.onSpawn(
        [&asked](Scene& scene, ResourceManager&, PlayerId) {
            return ++asked == 1 ? EntityId{} : scene.createEntity();
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 4, TEST_TICK_RATE);
    const uint16_t port = server.localAddress().port;

    Scene firstWorld, secondWorld;
    NetSession first, second;
    first.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto pump = [&]() {
        server.receive(serverWorld, resources);
        first.receive(firstWorld, resources);
        second.receive(secondWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        first.beginTick(tick, InputCommand{}, 3);
        second.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        first.send(firstWorld, tick);
        second.send(secondWorld, tick);
        server.advance(1.0f / 60.0f);
        first.advance(1.0f / 60.0f);
        second.advance(1.0f / 60.0f);
    };
    check("the first is declined", pumpUntil(pump, [&]() { return first.isDisconnected(); }));

    second.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);
    check("the second is seated", pumpUntil(pump, [&]() { return second.isPlaying(); }));
    check("  as player one, the number the declined join never took", second.localPlayer() == 1);

    second.close();
    server.close();
}

void testAClientThatLosesItsServerDoesNotInheritTheWorld() {
    std::printf("What a client decides once the server has gone:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 8);

    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId e = scene.createEntity();
            scene.add(e, Transform{});
            scene.add(e, Rigidbody{});
            return e;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 4, TEST_TICK_RATE);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check(
        "and the world arrives",
        pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); })
    );

    const EntityId mine   = client.localEntity();
    const EntityId theirs = clientWorld.entityAt(crates.back().slot());
    check("it decides its own", client.simulates(mine));
    check("and not a crate", !client.simulates(theirs));

    // The server goes quiet without a goodbye - its process gone, the ordinary end of a
    // game. Its socket stays open and unread, so nothing answers or refuses.
    for (int i = 0; i < 400 && !client.isDisconnected(); ++i) {
        client.receive(clientWorld, resources);
        client.advance(1.0f / 60.0f);
    }
    check("the client notices", client.isDisconnected());

    // Offline means "decide everything, nobody else does"; this end decided nothing and
    // just lost who did. Made Offline, it would take a world it had only been shown.
    check("it does not become the authority", !client.simulates(theirs));
    check("nor over what it used to drive", !client.simulates(mine));
    check("and it owns nothing now", !client.isMine(mine));
    check("which is not the same as never having played", !client.isOffline());
    check("and it says why", client.lastError() == std::string("the server stopped answering"));
}

void testAStutterDoesNotCostTheTicksItRan() {
    std::printf("A frame that ran thirty-two ticks, and one packet to say so:\n");

    // The clock runs up to Config::MAX_FRAME_ACCUMULATOR of ticks a frame - 32 at 128 Hz
    // - and a frame sends one packet, so a backlog beyond a packet is ordinary.
    std::vector<InputCommand> made;
    for (uint32_t i = 0; i < 32; ++i) {
        InputCommand c;
        c.sequence = i;
        c.tick     = 1000 + i;
        c.axis[0]  = 1.0f;
        if (i == 0) c.pressed = 1u << 2;   // the jump, on the oldest of them
        made.push_back(c);
    }

    NetCommandBuffer server;
    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;
    std::vector<InputCommand> pending = made;

    // Packet after packet, one frame's worth each, with nothing acknowledged: everything
    // the client ran must reach the server.
    int packets = 0;
    std::set<uint32_t> arrived;
    while (!pending.empty() && packets < 10) {
        BitWriter writer(packet, 1024);
        writeCommands(writer, pending, 0, 6);
        writer.finish();

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, 6, got));
        for (const InputCommand& c : got) server.accept(c);

        // What went is gone; the rest waits for the next frame.
        for (const InputCommand& c : got) arrived.insert(c.sequence);
        pending.erase(
            pending.begin(),
            pending.begin() + static_cast<long>(std::min(got.size(), pending.size()))
        );
        ++packets;
    }

    check("one packet could not hold them", packets > 1);
    check(
        "but every one of them reached the server, by sequence",
        arrived.size() == made.size() && *arrived.begin() == 0 && *arrived.rbegin() == made.size() - 1
    );
    check("with none left behind on the client", pending.empty());

    // A backlog past the queue's depth is skipped, since a queue is input delay: skipping
    // an axis is free, an edge is not, so skipped edges fold into the running command.
    // The jump is on the burst's oldest command.
    int jumps = 0;
    for (uint32_t tick = 0; tick < 40; ++tick) {
        if (server.take(2000 + tick).pressed & (1u << 2)) ++jumps;
    }
    check("and the jump on the oldest of them survived, exactly once", jumps == 1);
    std::printf("      32 ticks in one frame, all delivered across %d packets\n", packets);
}

// A command packet carries a fixed number of commands. Sliding that window only on run
// confirmations, a round trip late, trails the player by what the trip does not cover;
// on a long link the server runs input most of a second old. Sliding on what the
// server has heard keeps input one trip old.
void testALongRoundTripKeepsInputOneTripOld() {
    std::printf("A round trip twice as long as a packet's window of input:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);
    NetSession server;
    server.onSpawn(
        [walker](Scene&, ResourceManager&, PlayerId) { return walker; },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server binds", server.host(0, 2, TEST_TICK_RATE));

    // As many rounds each way as a packet resends commands: a round trip of twice that.
    constexpr int ONE_WAY = static_cast<int>(NET_COMMAND_REDUNDANCY);
    DelayedLink link(server.localAddress().port, ONE_WAY);
    check("the link opens", link.isOpen());

    Scene clientWorld;
    addWalker(clientWorld);
    NetSession client;
    check("the client opens a socket", client.connect(link.front(), TEST_TICK_RATE));

    uint32_t tick     = 0;
    uint32_t lastRun  = 0;
    uint32_t worstAge = 0;
    int      fresh    = 0;
    int      measured = 0;
    bool     counting = false;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;

        InputCommand walking;
        walking.sequence = tick;
        walking.tick     = tick;
        walking.axis[0]  = 1.0f;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walking, 3);

        // A command the server had not run, or the last one repeated for lack of new.
        const uint32_t ran = server.commandFor(walker).sequence;
        if (counting) {
            ++measured;
            if (ran != lastRun) ++fresh;
            worstAge = std::max(worstAge, tick - ran);
        }
        lastRun = ran;

        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        link.pump();
        server.advance(1.0f / NET_COMMAND_RATE);
        client.advance(1.0f / NET_COMMAND_RATE);
    };

    check("the client joins across it", pumpUntil(frame, [&]() { return client.isPlaying(); }, 200));

    // Long enough for what queued during the join to drain, then measured.
    for (int i = 0; i < 200; ++i) frame();
    counting = true;
    for (int i = 0; i < 256; ++i) frame();

    check("the server runs a command it had not run before on nearly every tick", fresh * 10 >= measured * 9);
    // One way plus the server's short queue; sliding on run confirmations instead trails
    // by most of a second.
    check("and none of them has waited a whole round trip", worstAge <= static_cast<uint32_t>(ONE_WAY * 2));
    std::printf(
        "      %d of %d ticks ran fresh input, the oldest %u ticks old, over a %d-tick round trip\n",
        fresh,
        measured,
        worstAge,
        ONE_WAY * 2
    );

    client.close();
    server.close();
}

// Every snapshot carries how low the server's queue ran, and it moves only a playing
// client's pacing: a server running two ticks per client tick finds it empty and
// hurries the client; a client making two per server tick backs it up until trimmed
// and is held back. Every other end ticks at wall-clock pace.
void testTheServersQueueSetsTheClientsPace() {
    std::printf("What the server's queue says to the client's clock:\n");

    NetSession offline;
    check("offline the pacing is exactly one", offline.pacing() == 1.0f);

    for (const bool starved : {true, false}) {
        const ScopedEngineSchema wire;
        ResourceManager resources;

        Scene serverWorld;
        const EntityId walker = addWalker(serverWorld);
        NetSession server;
        server.onSpawn(
            [walker](Scene&, ResourceManager&, PlayerId) { return walker; },
            [](Scene&, ResourceManager&, PlayerId, EntityId) {}
        );
        check("the server binds", server.host(0, 2, TEST_TICK_RATE));

        Scene clientWorld;
        addWalker(clientWorld);
        NetSession client;
        check(
            "the client opens a socket",
            client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE)
        );
        check("  and while it joins, its pacing is exactly one", client.pacing() == 1.0f);

        uint32_t serverTick = 0;
        uint32_t clientTick = 0;
        const auto frame = [&]() {
            server.receive(serverWorld, resources);
            client.receive(clientWorld, resources);
            for (int i = 0; i < (starved ? 2 : 1); ++i) {
                ++serverTick;
                server.beginTick(serverTick, InputCommand{}, 3);
                server.endTick(serverWorld, serverTick);
            }
            for (int i = 0; i < (starved ? 1 : 2); ++i) {
                ++clientTick;
                client.beginTick(clientTick, walkCommand(clientTick, clientTick, 1.0f, false), 3);
                client.endTick(clientWorld, clientTick);
            }
            server.send(serverWorld, serverTick);
            client.send(clientWorld, clientTick);
            server.advance(1.0f / NET_SNAPSHOT_RATE);
            client.advance(1.0f / NET_COMMAND_RATE);
        };

        check("  the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
        for (int i = 0; i < 200; ++i) frame();

        check("  the server's pacing is exactly one, whatever it hears", server.pacing() == 1.0f);
        if (starved) {
            check(
                "  a server starved of commands hurries the client",
                client.pacing() > 1.0f + NetPacing::MAX_DILATION * 0.5f
            );
        } else {
            check(
                "  a server backed up with them holds the client back",
                client.pacing() < 1.0f - NetPacing::MAX_DILATION * 0.5f
            );
        }
        std::printf(
            "      %s: the client ticks at %.3f of the wall clock\n",
            starved ? "starved" : "backed up",
            static_cast<double>(client.pacing())
        );

        client.close();
        server.close();
        check("  closed, the client's pacing is exactly one again", client.pacing() == 1.0f);
    }
}

// A source address is trivially forged, and the connection window moves on what it
// accepts. A datagram in a player's name, numbered far ahead, must be refused before it
// makes every real packet after it look stale.
void testAForgedPacketCannotTalkOverAPlayer() {
    std::printf("A datagram in a player's name that does not carry their token:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);
    NetSession server;
    server.onSpawn(
        [walker](Scene&, ResourceManager&, PlayerId) { return walker; },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    check("the server binds", server.host(0, 2, TEST_TICK_RATE));

    // No delay: the link lets the test speak from the client's address.
    DelayedLink link(server.localAddress().port, 0);
    check("the link opens", link.isOpen());

    Scene clientWorld;
    addWalker(clientWorld);
    NetSession client;
    client.connect(link.front(), TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walkCommand(tick, tick, 1.0f, false), 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        link.pump();
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));
    for (int i = 0; i < 20; ++i) frame();

    // Sixteen thousand packets ahead, with a token not the seat's. Accepted, every real
    // packet after it would read as older and be refused.
    std::vector<uint8_t> forged(NetConnection::HEADER_BYTES, 0u);
    forged[0] = 0x00;
    forged[1] = 0x40;
    std::vector<uint8_t> body;
    BitWriter writer(body, 32);
    writer.u16(NET_PROTOCOL_TAG);
    writer.u8(NET_PROTOCOL_VERSION);
    writer.u8(static_cast<uint8_t>(NetMessage::Command));
    writer.u64(0x0123456789ABCDEFull);
    writer.finish();
    forged.insert(forged.end(), body.begin(), body.end());
    link.forge(forged);

    // Past NetConnection::TIMEOUT_SECONDS: a player with real packets refused would have
    // lost the seat by now.
    for (int i = 0; i < 400; ++i) frame();
    check("the player keeps the seat", server.playerCount() == 1);
    check("  and the client is still playing", client.isPlaying());

    client.close();
    server.close();
}

// Anyone can send a Hello from any address; it must buy nothing the game pays for - a
// seat, or a player spawned for somebody who cannot hear.
void testAHelloThatNeverAnswersTakesNoSeat() {
    std::printf("A join from an address that never hears the reply:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    int spawned = 0;
    NetSession server;
    server.onSpawn(
        [&spawned](Scene& scene, ResourceManager&, PlayerId) {
            ++spawned;
            return scene.createEntity();
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    check("a one-seat server binds", server.host(0, 1, TEST_TICK_RATE));

    // Says Hello and never reads its socket: to the server, a forged source address.
    Scene ghostWorld;
    NetSession ghost;
    ghost.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);
    ghost.beginTick(1, InputCommand{}, 3);
    ghost.send(ghostWorld, 1);

    for (int i = 0; i < 5; ++i) {
        server.receive(serverWorld, resources);
        server.advance(1.0f / 60.0f);
    }
    check("the Hello seats nobody", server.playerCount() == 0);
    check("  and the game spawned nothing for it", spawned == 0);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);
    uint32_t tick = 1;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check("a real player still gets the one seat", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("  spawned once, for them", spawned == 1 && server.playerCount() == 1);

    ghost.close();
    client.close();
    server.close();
}

// Anyone can send Hellos from any address, as fast as they like. Keeping a record per
// join until it proved itself would need a bound, and a flood would push every real
// join out before its answer came back. Nothing is kept, so nothing can be pushed.
void testAFloodOfHellosCrowdsOutNoRealJoin() {
    std::printf("A real join behind a flood of Hellos from addresses that never answer:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    check("a server binds", server.host(0, 4, TEST_TICK_RATE));
    const NetAddress at{0x7F000001u, server.localAddress().port};

    // None reads its socket, so to the server each is a forged source that keeps asking.
    constexpr int FLOOD = 40;
    std::vector<std::unique_ptr<NetSession>> ghosts;
    std::vector<Scene> ghostWorlds(FLOOD);
    for (int i = 0; i < FLOOD; ++i) {
        ghosts.push_back(std::make_unique<NetSession>());
        ghosts.back()->connect(at, TEST_TICK_RATE);
    }

    Scene clientWorld;
    NetSession client;
    client.connect(at, TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        // Between Challenge and answer, every frame: where a bounded wait list would be
        // weakest.
        for (int i = 0; i < FLOOD; ++i) {
            ghosts[i]->beginTick(tick, InputCommand{}, 3);
            ghosts[i]->send(ghostWorlds[i], tick);
            ghosts[i]->advance(1.0f / 60.0f);
        }
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the real player joins", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));
    for (int i = 0; i < 30; ++i) frame();
    check("  and keeps the seat through the flood", client.isPlaying() && server.playerCount() == 1);
    check("  the only seat taken", server.playerCount() == 1);

    for (auto& ghost : ghosts) ghost->close();
    client.close();
    server.close();
}

// A join token belongs to a time window. A client still saying Hello as the window
// turns can get two tokens, be seated by the first, then hold the second - and drop
// every Welcome as a forgery.
void testAJoinAcrossAWindowsTurnIsStillSeated() {
    std::printf("A join whose Challenges straddle a token window:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    check("a server binds", server.host(0, 4, TEST_TICK_RATE));

    // Just short of the window's turn.
    server.advance(static_cast<float>(NetJoinCookie::WINDOW_SECONDS) - 0.01f);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);
    client.beginTick(1, InputCommand{}, 3);
    const auto say = [&]() {
        client.advance(1.0f / 60.0f);
        client.send(clientWorld, 1);
    };

    say();                                          // Hello, no token
    server.receive(serverWorld, resources);         // Challenge, this window's token
    say();                                          // Hello, no token, still in flight
    client.receive(clientWorld, resources);         // takes the first token
    server.advance(0.02f);                          // the window turns
    server.receive(serverWorld, resources);         // Challenge, the next window's token
    say();                                          // Hello echoing the first token
    server.receive(serverWorld, resources);         // seated by it, and Welcomed
    client.receive(clientWorld, resources);         // takes the second token; the Welcome is not its
    check(
        "the client was seated by one token and holds the other",
        server.playerCount() == 1 && !client.isPlaying()
    );

    uint32_t tick = 1;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check(
        "it still finishes joining, well inside the timeout",
        pumpUntil(frame, [&]() { return client.isPlaying(); }, 20)
    );
    check("  in the one seat it was given", server.playerCount() == 1);

    client.close();
    server.close();
}

// A snapshot header cut short reads as zeros, and server tick zero is the one value
// that disables the guard against unreachable ticks. Believed, the next snapshot can
// claim any tick, sending the draw clock where no real sample reaches.
void testASnapshotCutShortIsNotBelieved() {
    std::printf("A snapshot that ends inside its own header:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 3);
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId e = scene.createEntity();
            scene.add(e, Transform{});
            scene.add(e, Rigidbody{});
            return e;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 2, TEST_TICK_RATE);

    DelayedLink link(server.localAddress().port, 0);
    check("the link opens", link.isOpen());

    Scene clientWorld;
    NetSession client;
    client.connect(link.front(), TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.endTick(serverWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        link.pump();
        client.interpolate(clientWorld, 1.0f / 60.0f);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));
    const EntityId crate = crates.back();
    check(
        "and the world arrives",
        pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crate.slot()); }, 80)
    );
    for (int i = 0; i < 10; ++i) frame();

    // The newest real snapshot replayed under the next two sequence numbers: once cut off
    // inside its tick, once claiming a tick past any jump a client accepts.
    const std::vector<uint8_t> real = link.lastToClient();
    const size_t payload = NetConnection::HEADER_BYTES;
    check(
        "the newest datagram is a snapshot",
        real.size() > payload + 24 && real[payload + 3] == static_cast<uint8_t>(NetMessage::Snapshot)
    );
    const auto renumber = [](std::vector<uint8_t>& bytes, uint16_t sequence) {
        bytes[0] = static_cast<uint8_t>(sequence & 0xFF);
        bytes[1] = static_cast<uint8_t>(sequence >> 8);
    };
    const uint16_t sequence = static_cast<uint16_t>(real[0] | (real[1] << 8));

    std::vector<uint8_t> cut(real.begin(), real.begin() + static_cast<long>(payload + 14));
    renumber(cut, static_cast<uint16_t>(sequence + 1));
    link.forgeToClient(cut);

    std::vector<uint8_t> ahead = real;
    renumber(ahead, static_cast<uint16_t>(sequence + 2));
    uint32_t claimed = 0;
    for (int i = 0; i < 4; ++i) claimed |= static_cast<uint32_t>(ahead[payload + 12 + i]) << (8 * i);
    claimed += NET_MAX_TICK_JUMP * 2;
    for (int i = 0; i < 4; ++i) ahead[payload + 12 + i] = static_cast<uint8_t>(claimed >> (8 * i));
    link.forgeToClient(ahead);

    // The world must still be drawn as it moves.
    serverWorld.get<Transform>(crate).position.x = 50.0f;
    const EntityId mirror = clientWorld.entityAt(crate.slot());
    const auto drawnMoved = [&]() {
        return std::abs(clientWorld.get<Transform>(mirror).position.x - 50.0f) < 0.01f;
    };
    check("a body the server moves afterwards is still drawn moving", pumpUntil(frame, drawnMoved, 120));

    client.close();
    server.close();
}

void testAServerThatHasBeenUpAWhileStillTakesAJoin() {
    std::printf("A server nobody restarted, and a client that has just started:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 6);

    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId e = scene.createEntity();
            scene.add(e, Transform{});
            scene.add(e, Rigidbody{});
            return e;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 4, TEST_TICK_RATE);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    // The server's tick counts from its own start and a fresh client's is near zero -
    // unrelated clocks. Four minutes at 128 Hz is past NET_MAX_TICK_JUMP.
    uint32_t serverTick = Config::MAX_TICK_RATE * 60 + 5000;
    uint32_t clientTick = 0;

    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++serverTick;
        ++clientTick;
        server.beginTick(serverTick, InputCommand{}, 0);
        client.beginTick(clientTick, InputCommand{}, 0);
        server.endTick(serverWorld, serverTick);
        client.endTick(clientWorld, clientTick);
        server.send(serverWorld, serverTick);
        client.send(clientWorld, clientTick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client is welcomed", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));

    // The world must arrive: refusing a header for a tick this end has not reached
    // leaves the client connected, seated, and blind.
    const bool arrived = pumpUntil(
        frame,
        [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); },
        200
    );
    check("and the world it joined arrives", arrived);
    std::printf(
        "      server at tick %u, client at tick %u, %zu entities across\n",
        serverTick,
        clientTick,
        clientWorld.entityCount()
    );

    client.close();
    server.close();
}

void testStandingIsToldRatherThanGuessed() {
    std::printf("The five micrometres that would leave every other player mid-jump:\n");

    const ScopedEngineSchema wire;

    // A floor and a resting character built identically on both ends: a slot is an
    // entity's wire name, and both loaded the same scene.
    const auto addWorld = [](Scene& scene, EntityId& floor) {
        floor = addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
        const EntityId walker = scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.0f, 0.0f};
        scene.add(walker, at);

        Rigidbody body;
        body.mass           = 70.0f;
        body.freezeRotation = true;
        scene.add(walker, body);

        Collider capsule;
        ColliderPart part;
        part.shape      = ColliderShape::Capsule;
        part.radius     = 0.3f;
        part.halfHeight = 0.6f;
        part.center     = {0.0f, 0.9f, 0.0f};
        capsule.parts   = {part};
        scene.add(walker, capsule);

        scene.add(walker, CharacterController{});
        return walker;
    };

    Scene serverWorld;
    EntityId serverFloor;
    const EntityId walker = addWorld(serverWorld, serverFloor);

    NetSession server;
    server.onSpawn(
        [&](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId spare = scene.createEntity();
            scene.add(spare, Transform{});
            scene.add(spare, Rigidbody{});
            return spare;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 2, TEST_TICK_RATE);

    Scene clientWorld;
    EntityId clientFloor;
    const EntityId clientWalker = addWorld(clientWorld, clientFloor);
    (void)clientFloor;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    PhysicsSystem serverPhysics, clientPhysics;
    CharacterControllerSystem serverWalk, clientWalk;
    TestFrame serverFrame(serverWorld, server);
    TestFrame clientFrame(clientWorld, client);
    FrameContext& serverCtx = serverFrame.ctx;
    FrameContext& clientCtx = clientFrame.ctx;
    ResourceManager& resources = serverFrame.resources;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        serverWalk.fixedUpdate(serverCtx);
        clientWalk.fixedUpdate(clientCtx);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    for (int i = 0; i < 30; ++i) frame();

    check("the server has the character standing", serverWorld.get<CharacterController>(walker).grounded);
    check("and the client does not decide this one", !client.simulates(clientWalker));

    // Leave the client nothing to measure. Collider enablement does not replicate, so
    // this sticks where moving the floor would not; real scenes get the gap from rounding.
    clientWorld.get<Collider>(clientWalker).enabled = false;
    for (int i = 0; i < 30; ++i) frame();

    // Gravity renews a resting capsule's contact every tick, and a client applies none
    // to a body it does not decide - so it can find nothing under a well-placed character.
    check(
        "the client finds nothing under the character",
        !clientWorld.get<Rigidbody>(clientWalker).supported
    );
    check(
        "and is told it is standing all the same",
        clientWorld.get<CharacterController>(clientWalker).grounded
    );

    client.close();
    server.close();
}

void testAClientDoesNotMoveWhatItDoesNotOwn() {
    std::printf("Why two clients see the same tower fall the same way:\n");

    const ScopedEngineSchema wire;

    const auto addCrate = [](Scene& scene, float y) {
        const EntityId crate = scene.createEntity();
        Transform at;
        at.position = {0.0f, y, 0.0f};
        scene.add(crate, at);
        Rigidbody body;
        body.mass = 2.0f;
        scene.add(crate, body);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = {0.5f, 0.5f, 0.5f};
        box.parts        = {part};
        scene.add(crate, box);
        return crate;
    };

    Scene serverWorld;
    const EntityId crate = addCrate(serverWorld, 5.0f);

    NetSession server;
    server.onSpawn(
        [&](Scene& scene, ResourceManager&, PlayerId) { return addCrate(scene, 20.0f); },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    server.host(0, 2, TEST_TICK_RATE);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    PhysicsSystem serverPhysics, clientPhysics;
    TestFrame serverFrame(serverWorld, server);
    TestFrame clientFrame(clientWorld, client);
    FrameContext& serverCtx = serverFrame.ctx;
    FrameContext& clientCtx = clientFrame.ctx;
    ResourceManager& resources = serverFrame.resources;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 0);
        client.beginTick(tick, InputCommand{}, 0);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        // A frame is time passing, which clocks the wire - not send calls - so a test
        // that never advances time gets one packet and then silence, correctly.
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and is a client, not an authority", client.role() == NetRole::Client);

    check(
        "the crate reaches it",
        pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crate.slot()); })
    );
    const EntityId mirror = clientWorld.entityAt(crate.slot());
    check("and does not decide the crate's fate", !client.simulates(mirror));
    check("while it does decide its own", client.simulates(client.localEntity()));

    for (int i = 0; i < 30; ++i) frame();

    const float serverY = serverWorld.get<Transform>(crate).position.y;
    const float clientY = clientWorld.get<Transform>(mirror).position.y;
    check("the server's crate fell", serverY < 4.9f);

    // The client trails the server by about a packet's travel - it draws a past the
    // server has left - but must not invent a present of its own.
    check("the client is behind the server, not ahead of it", clientY >= serverY);
    // Behind by about the snapshot gap: at NET_SNAPSHOT_RATE, a body half a second into
    // a fall moves under a decimetre between them.
    check("behind by about the gap between snapshots, not by a guess", clientY - serverY < 0.3f);
    std::printf(
        "      server %.3f m, client %.3f m, %.0f mm apart\n",
        serverY,
        clientY,
        (clientY - serverY) * 1000.0f
    );

    // With the server silent, a client simulating the crate would keep dropping it - and
    // two clients dropping it from different moments is "we see different worlds".
    const float before = clientWorld.get<Transform>(mirror).position.y;
    for (int i = 0; i < 60; ++i) {
        ++tick;
        client.beginTick(tick, InputCommand{}, 0);
        clientPhysics.fixedUpdate(clientCtx);
    }
    check(
        "with nothing arriving, the client does not move it a hair",
        std::abs(clientWorld.get<Transform>(mirror).position.y - before) < 1e-6f
    );

    client.close();
    server.close();
}

void testAClientPredictsWhatItIsPushing() {
    std::printf("What a client is allowed to move because it is leaning on it:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    const auto addBox = [](Scene& scene, glm::vec3 at, glm::vec3 half, RigidbodyMotion motion) {
        const EntityId body = scene.createEntity();
        Transform where;
        where.position = at;
        scene.add(body, where);
        Rigidbody rb;
        rb.mass   = 1.0f;
        rb.motion = motion;
        scene.add(body, rb);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = half;
        box.parts        = {part};
        scene.add(body, box);
        return body;
    };

    // A floor, a crate on a wider one, and a far crate. The player is dropped onto the
    // near crate, beside another player's character wearing a hat.
    EntityId floorId, baseCrate, nearCrate, farCrate, player, other, hat;
    const auto build = [&](Scene& scene) {
        floorId   = addBox(scene, {0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 40.0f}, RigidbodyMotion::Static);
        baseCrate = addBox(scene, {0.0f,  0.5f, 0.0f}, {1.5f, 0.5f, 0.5f}, RigidbodyMotion::Dynamic);
        nearCrate = addBox(scene, {0.0f,  1.5f, 0.0f}, {1.0f, 0.5f, 0.5f}, RigidbodyMotion::Dynamic);
        farCrate  = addBox(scene, {20.0f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, RigidbodyMotion::Dynamic);
        player    = addBox(scene, {-0.5f, 2.6f, 0.0f}, {0.4f, 0.4f, 0.4f}, RigidbodyMotion::Dynamic);
        other     = addBox(scene, {0.5f,  2.6f, 0.0f}, {0.4f, 0.4f, 0.4f}, RigidbodyMotion::Dynamic);
        scene.add(other, CharacterController{});
        hat       = addBox(scene, {0.5f,  3.3f, 0.0f}, {0.25f, 0.25f, 0.25f}, RigidbodyMotion::Dynamic);
        return player;
    };

    Scene serverWorld;
    build(serverWorld);
    const EntityId serverPlayer = player;
    const EntityId serverBase   = baseCrate;
    const EntityId serverNear   = nearCrate;
    const EntityId serverFar    = farCrate;
    const EntityId serverFloor  = floorId;
    const EntityId serverOther  = other;
    const EntityId serverHat    = hat;

    NetSession server;
    server.onSpawn(
        [serverPlayer](Scene&, ResourceManager&, PlayerId) { return serverPlayer; },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    server.host(0, 2, TEST_TICK_RATE);

    Scene clientWorld;
    build(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    PhysicsSystem serverPhysics, clientPhysics;
    TestFrame serverFrame(serverWorld, server);
    TestFrame clientFrame(clientWorld, client);
    FrameContext& serverCtx = serverFrame.ctx;
    FrameContext& clientCtx = clientFrame.ctx;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        serverPhysics.fixedUpdate(serverCtx);
        clientPhysics.fixedUpdate(clientCtx);
        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and drives the player", client.localEntity().slot() == serverPlayer.slot());

    // Let it settle onto the crate.
    check(
        "and comes to rest on the crate it was dropped on",
        pumpUntil(frame, [&]() { return client.leaseCount() > 0; }, 200)
    );

    // Settled, so every contact the stack will have is there.
    for (int i = 0; i < 60; ++i) frame();

    const EntityId clientBase  = clientWorld.entityAt(serverBase.slot());
    const EntityId clientNear  = clientWorld.entityAt(serverNear.slot());
    const EntityId clientFar   = clientWorld.entityAt(serverFar.slot());
    const EntityId clientFloor = clientWorld.entityAt(serverFloor.slot());
    const EntityId clientOther = clientWorld.entityAt(serverOther.slot());
    const EntityId clientHat   = clientWorld.entityAt(serverHat.slot());

    check("the crate it is standing on is now its to move", client.simulates(clientNear));
    check("  and the crate that one rests on", client.simulates(clientBase));
    check("but it is still not the client's own entity", !client.isMine(clientNear));
    check("the floor is not: the closure stops at what cannot move", !client.simulates(clientFloor));
    check("nor another player's character on the same crate", !client.simulates(clientOther));
    check("  nor what rests on that character alone", !client.simulates(clientHat));
    check("and neither is a crate on the other side of the level", !client.simulates(clientFar));
    check("  so it leases those two and nothing else", client.leaseCount() == 2);
    std::printf(
        "      leasing %zu body(s) of %zu in the world\n",
        client.leaseCount(),
        clientWorld.entityCount()
    );

    // Take the contact away: the lease must lapse, not hold for the rest of the match.
    clientWorld.get<Transform>(client.localEntity()).position = {0.0f, 30.0f, 0.0f};
    serverWorld.get<Transform>(serverPlayer).position         = {0.0f, 30.0f, 0.0f};
    bool lapsed = false;
    for (int i = 0; i < 200 && !lapsed; ++i) {
        frame();
        lapsed = !client.simulates(clientNear);
    }
    check("and once nothing is touching it, the lease lapses", lapsed);

    client.close();
    server.close();
}

void testAWorldConvergesThroughALossyLink() {
    std::printf("A third of every packet lost, reordered and duplicated:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 60);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();

    Scene client;

    // Seeded, so a failure repeats every run; which packets are lost should not matter.
    Math::Rng noise(12345u);
    const auto roll = [&noise](int upTo) { return noise.nextInt(0, upTo - 1); };

    // Both ends of a real connection, so arrivals pass a client's check (older than one
    // taken is refused) and server confirmations follow the client's acknowledgements.
    NetConnection serverEnd, clientEnd;
    serverEnd.open(NetAddress{0x7F000001u, 1});
    clientEnd.open(NetAddress{0x7F000001u, 2});

    std::vector<uint8_t>  payload;
    std::vector<uint8_t>  datagram;
    std::vector<uint16_t> acknowledged;

    const auto arrive = [&](const std::vector<uint8_t>& bytes) {
        acknowledged.clear();
        if (!clientEnd.accept(bytes.data(), bytes.size(), payload, acknowledged)) return false;
        if (!readSnapshotFrom(client, schema, payload)) {
            clientEnd.refuse();
            return false;
        }
        return true;
    };
    const auto answer = [&]() {
        clientEnd.frame(nullptr, 0, datagram);
        acknowledged.clear();
        if (!serverEnd.accept(datagram.data(), datagram.size(), payload, acknowledged)) return;
        for (uint16_t confirmed : acknowledged) baseline.confirm(confirmed);
    };
    const auto writeOne = [&]() {
        std::vector<uint8_t> body;
        writeSnapshotTo(server, schema, serverEnd.nextSequence(), budget, baseline, body);
        serverEnd.frame(body.data(), body.size(), datagram);
        return datagram;
    };

    // Written but undelivered datagrams, so they can arrive late, twice or never - what
    // a network does and the acknowledgement design must survive.
    std::vector<std::vector<uint8_t>> wire;
    int sent = 0, lost = 0, delivered = 0, stale = 0, duplicated = 0;

    for (int round = 0; round < 400; ++round) {
        // Moving for the first half, then settling - when a frozen body would show, since
        // nothing later would correct it.
        if (round < 200) {
            for (size_t i = 0; i < crates.size(); ++i) {
                if (roll(3) != 0) continue;
                server.get<Transform>(crates[i]).position +=
                    glm::vec3(0.05f, 0.0f, 0.02f);
            }
        }

        wire.push_back(writeOne());
        ++sent;

        // Delivery, out of order and unreliable.
        for (size_t i = wire.size(); i-- > 0;) {
            const int die = roll(100);
            if (die < 33) {                       // lost outright
                wire.erase(wire.begin() + static_cast<long>(i));
                ++lost;
                continue;
            }
            if (die < 50 && wire.size() < 8) continue;   // held, arrives later

            if (arrive(wire[i])) ++delivered;
            else                 ++stale;

            if (die > 95) {                       // and again, as a duplicate
                arrive(wire[i]);
                ++duplicated;
            }
            wire.erase(wire.begin() + static_cast<long>(i));
        }
        answer();
    }

    // Drain whatever is still in flight, then let it settle.
    for (int round = 0; round < 40; ++round) {
        arrive(writeOne());
        answer();
    }

    check("packets really were lost", lost > sent / 5);
    check("some arrived behind a newer one, and were refused as the client refuses them", stale > 0);
    check("and some arrived twice", duplicated > 0);

    // After all that the two worlds are the same - to the millimetre the wire carries.
    float worst = 0.0f;
    int missing = 0;
    for (EntityId crate : crates) {
        if (!client.isAliveAtIndex(crate.slot())) {
            ++missing;
            continue;
        }
        const glm::vec3 there = client.get<Transform>(client.entityAt(crate.slot())).position;
        const glm::vec3 here  = server.get<Transform>(crate).position;
        worst = std::max(worst, glm::length(there - here));
    }
    check("every body is on the client", missing == 0);
    check("and every one of them is where the server has it", worst < 0.002f);
    std::printf(
        "      %d sent, %d lost, %d delivered, %d refused as stale, %d duplicated; "
        "worst body off by %.4f mm\n",
        sent,
        lost,
        delivered,
        stale,
        duplicated,
        static_cast<double>(worst) * 1000.0
    );

    // Not by shouting: a settled world under loss still goes quiet, as presence is
    // measured against what was confirmed.
    std::vector<uint8_t> body;
    const NetSnapshotStats quiet =
        writeSnapshotTo(server, schema, serverEnd.nextSequence(), budget, baseline, body);
    check("a settled world is quiet even after all that", quiet.entitiesWritten <= 1);
}

} // namespace

void runNetSessionTests() {
    testTwoSessionsPlayTheSameGame();
    testAClientBuiltFromDifferentSourceIsTurnedAway();
    testTheSameActionsInAnotherOrderAreTurnedAway();
    testAClientAtAnotherTickRateIsTurnedAway();
    testAnotherBuildIsToldRatherThanIgnored();
    testBothEndsHoldTheScaleTheWireCarries();
    testOneWorldHasOneFingerprintOnEitherPlatform();
    testAFullServerSaysSoRatherThanIgnoring();
    testADeclinedJoinSpendsNoPlayerNumber();
    testAClientThatLosesItsServerDoesNotInheritTheWorld();
    testAStutterDoesNotCostTheTicksItRan();
    testALongRoundTripKeepsInputOneTripOld();
    testTheServersQueueSetsTheClientsPace();
    testAForgedPacketCannotTalkOverAPlayer();
    testAHelloThatNeverAnswersTakesNoSeat();
    testAFloodOfHellosCrowdsOutNoRealJoin();
    testAJoinAcrossAWindowsTurnIsStillSeated();
    testASnapshotCutShortIsNotBelieved();
    testAServerThatHasBeenUpAWhileStillTakesAJoin();
    testStandingIsToldRatherThanGuessed();
    testAClientDoesNotMoveWhatItDoesNotOwn();
    testAClientPredictsWhatItIsPushing();
    testAWorldConvergesThroughALossyLink();
}
