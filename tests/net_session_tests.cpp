#include "support.h"

namespace {

void testTwoSessionsPlayTheSameGame() {
    std::printf("A server and a client, on real sockets, joining and playing:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 30);

    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId id) {
            const EntityId player = scene.createEntity();
            Transform at;
            at.position = {static_cast<float>(id) * 3.0f, 1.0f, -5.0f};
            scene.add(player, at);
            scene.add(player, Rigidbody{});
            return player;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId entity) { scene.destroyEntity(entity); });

    check("the server binds a port", server.host(0, 4));
    check("and is refereeing straight away", server.isPlaying());
    check("a host drives nobody, so every player is a real client",
          !server.localEntity());

    Scene clientWorld;
    NetSession client;
    check("the client opens a socket", client.connect(NetAddress{0x7F000001u, server.localAddress().port}));
    check("but is not playing until it is welcomed", !client.isPlaying());

    // Three actions, as a project defines. Both ends agree because both run
    // the same project.
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

    check("the world arrives",
          pumpUntil(frame, [&]() { return clientWorld.entityCount() >= 31; }, 80));
    std::printf("      %zu entities on the server, %zu on the client\n",
                serverWorld.entityCount(), clientWorld.entityCount());

    // The crates the server built are where the server has them.
    const EntityId probe = crates[17];
    const EntityId mirror = clientWorld.entityAt(probe.slot());
    check("a crate the client was never told about directly is there",
          clientWorld.isAlive(mirror));
    check("at the position the server has it",
          std::abs(clientWorld.get<Transform>(mirror).position.x
                   - serverWorld.get<Transform>(probe).position.x) < 0.002f);

    // The server moves something. It has to reach the client without anything
    // being asked for.
    serverWorld.get<Transform>(probe).position = {-77.5f, 12.25f, 3.0f};
    check("a change on the server reaches the client",
          pumpUntil(frame, [&]() {
              return std::abs(clientWorld.get<Transform>(mirror).position.x + 77.5f) < 0.002f;
          }));

    // Ownership: the client predicts its own entity and nothing else, which is
    // what stops it shoving every crate its own way and being corrected.
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

    const EntityId playerOnServer = server.players().front().entity;
    check("the server knows the player it is refereeing", bool(playerOnServer));
    const InputCommand& ran = server.commandFor(playerOnServer);
    check("the server runs the client's input on the client's entity",
          std::abs(ran.axis[0] - 1.0f) < 0.01f);
    check("with the edge intact", ran.pressed == (1u << 2));
    check("and nothing for an entity no player drives",
          server.commandFor(probe).pressed == 0);

    // Leaving is said, not waited for.
    client.close();
    server.receive(serverWorld, resources);
    for (int i = 0; i < 3; ++i) { server.advance(2.0f); server.receive(serverWorld, resources); }
    check("the seat is freed when a player leaves", server.playerCount() == 0);

    server.close();
}

void testAClientBuiltFromDifferentSourceIsTurnedAway() {
    std::printf("What happens when two ends do not agree on what a world is:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    check("the server is up", server.host(0, 4));

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The client says hello carrying a fingerprint of what it replicates, and
    // the datagram is on the wire before the list changes - so the server reads
    // what this build sent, which in the real world is simply another build.
    client.beginTick(1, InputCommand{}, 3);
    client.send(clientWorld, 1);

    NetSchema::get().replicate<Transform>("Health");

    bool refused = false;
    for (int i = 0; i < 20 && !refused; ++i) {
        server.receive(serverWorld, resources);
        server.send(serverWorld, 1);
        client.receive(clientWorld, resources);
        refused = client.isOffline() && !client.lastError().empty();
    }
    check("the client is refused rather than left to decode nonsense", refused);
    check("and told why", client.lastError() == std::string("a different build or world"));
    check("no seat was taken", server.playerCount() == 0);

    server.close();
}

void testAFullServerSaysSoRatherThanIgnoring() {
    std::printf("A server with one seat, and two players who want it:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;
    Scene serverWorld;
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 1);
    const uint16_t port = server.localAddress().port;

    Scene firstWorld, secondWorld;
    NetSession first, second;
    first.connect(NetAddress{0x7F000001u, port});

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

    check("the first player gets the seat",
          pumpUntil(pump, [&]() { return first.isPlaying(); }));

    second.connect(NetAddress{0x7F000001u, port});
    bool told = false;
    for (int i = 0; i < 20 && !told; ++i) {
        pump();
        told = second.isOffline() && !second.lastError().empty();
    }
    check("the second is told the game is full", told);
    check("in words a player can be shown",
          second.lastError() == std::string("the server is full"));
    check("and the first player is undisturbed", first.isPlaying());

    first.close();
    server.close();
}

void testAClientThatLosesItsServerDoesNotInheritTheWorld() {
    std::printf("What a client decides once the server has gone:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 8);

    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId e = scene.createEntity();
                       scene.add(e, Transform{});
                       scene.add(e, Rigidbody{});
                       return e;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 4);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

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
    check("and the world arrives",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); }));

    const EntityId mine   = client.localEntity();
    const EntityId theirs = clientWorld.entityAt(crates.back().slot());
    check("it decides its own", client.simulates(mine));
    check("and not a crate", !client.simulates(theirs));

    // The server goes quiet without saying goodbye - the process is gone,
    // which is the ordinary way a game ends. Its socket stays open and
    // unattended, so nothing answers and nothing refuses.
    for (int i = 0; i < 400 && !client.isDisconnected(); ++i) {
        client.receive(clientWorld, resources);
        client.advance(1.0f / 60.0f);
    }
    check("the client notices", client.isDisconnected());

    // The half that matters. Offline means "decide everything, there is nobody
    // else"; this end decided nothing all session and has just lost the one who
    // did. Told it was Offline it would take a world it had only been shown.
    check("it does not become the authority", !client.simulates(theirs));
    check("nor over what it used to drive", !client.simulates(mine));
    check("and it owns nothing now", !client.isMine(mine));
    check("which is not the same as never having played", !client.isOffline());
    check("and it says why", client.lastError() == std::string("the server stopped answering"));
}

void testAStutterDoesNotCostTheTicksItRan() {
    std::printf("A frame that ran thirty-two ticks, and one packet to say so:\n");

    // The clock runs up to a quarter second of ticks in one frame - thirty-two at
    // 128 Hz - and a frame sends one packet. More commands than a packet holds is
    // ordinary after any stall.
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

    // Packet after packet, each carrying what one frame could, with the server
    // acknowledging nothing. Everything the client ran has to reach it.
    int packets = 0;
    std::set<uint32_t> arrived;
    while (!pending.empty() && packets < 10) {
        BitWriter writer(packet, 1024);
        writeCommands(writer, pending, 6);
        writer.finish();

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, 6, got));
        for (const InputCommand& c : got) server.accept(c);

        // What went is gone; the rest waits for the next frame.
        for (const InputCommand& c : got) arrived.insert(c.sequence);
        pending.erase(pending.begin(),
                      pending.begin() + static_cast<long>(std::min(got.size(), pending.size())));
        ++packets;
    }

    check("one packet could not hold them", packets > 1);
    check("but every one of them reached the server, by sequence",
          arrived.size() == made.size() && *arrived.begin() == 0
          && *arrived.rbegin() == made.size() - 1);
    check("with none left behind on the client", pending.empty());

    // A backlog drains faster than it fills, because a queue is input delay:
    // skipping an axis is free, an edge is not, so skipped edges fold into the
    // command that runs. The jump is on the oldest command of the burst.
    int jumps = 0;
    for (uint32_t tick = 0; tick < 40; ++tick) {
        if (server.take(2000 + tick).pressed & (1u << 2)) ++jumps;
    }
    check("and the jump on the oldest of them survived, exactly once", jumps == 1);
    std::printf("      32 ticks in one frame, all delivered across %d packets\n", packets);
}

void testAServerThatHasBeenUpAWhileStillTakesAJoin() {
    std::printf("A server nobody restarted, and a client that has just started:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 6);

    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId e = scene.createEntity();
                       scene.add(e, Transform{});
                       scene.add(e, Rigidbody{});
                       return e;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 4);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The server has been running a while and its tick counts from its own start,
    // while a fresh client is near zero - the two clocks are unrelated. Four minutes
    // at 128 Hz is past the jump a client refuses a header for.
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

    // The world has to arrive. A header refused for claiming a tick this end
    // has not reached is a client that is connected, seated, and blind.
    const bool arrived = pumpUntil(frame,
        [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); }, 200);
    check("and the world it joined arrives", arrived);
    std::printf("      server at tick %u, client at tick %u, %zu entities across\n",
                serverTick, clientTick, clientWorld.entityCount());

    client.close();
    server.close();
}

void testStandingIsToldRatherThanGuessed() {
    std::printf("The five micrometres that had every other player mid-jump:\n");

    const ScopedEngineSchema wire;

    // A floor and a character resting on it, built identically on both ends -
    // a slot is an entity's name on the wire, and the two agree by having
    // loaded the same scene.
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
    server.onSpawn([&](Scene& scene, ResourceManager&, PlayerId) {
                       const EntityId spare = scene.createEntity();
                       scene.add(spare, Transform{});
                       scene.add(spare, Rigidbody{});
                       return spare;
                   },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 2);

    Scene clientWorld;
    EntityId clientFloor;
    const EntityId clientWalker = addWorld(clientWorld, clientFloor);
    (void)clientFloor;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

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

    check("the server has the character standing",
          serverWorld.get<CharacterController>(walker).grounded);
    check("and the client does not decide this one",
          !client.simulates(clientWalker));

    // Leave the client nothing to measure. Whether a collider is on does not
    // replicate, so this sticks where moving the floor would not. In a real
    // scene the same gap opens from rounding, less neatly.
    clientWorld.get<Collider>(clientWalker).enabled = false;
    for (int i = 0; i < 30; ++i) frame();

    // Gravity renews the contact under a resting capsule every tick, and a
    // client applies none to a body it does not decide - so it can see the
    // character in the right place and find nothing under it.
    check("the client finds nothing under the character",
          !clientWorld.get<Rigidbody>(clientWalker).supported);
    check("and is told it is standing all the same",
          clientWorld.get<CharacterController>(clientWalker).grounded);

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
    server.onSpawn([&](Scene& scene, ResourceManager&, PlayerId) { return addCrate(scene, 20.0f); },
                   [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); });
    server.host(0, 2);

    Scene clientWorld;
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

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
        // A frame is time passing. The wire is clocked by that rather than by
        // how often anybody calls send, so a test that never says time passed
        // gets one packet and then silence - correctly.
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and is a client, not an authority", client.role() == NetRole::Client);

    check("the crate reaches it",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crate.slot()); }));
    const EntityId mirror = clientWorld.entityAt(crate.slot());
    check("and does not decide the crate's fate", !client.simulates(mirror));
    check("while it does decide its own", client.simulates(client.localEntity()));

    for (int i = 0; i < 30; ++i) frame();

    const float serverY = serverWorld.get<Transform>(crate).position.y;
    const float clientY = clientWorld.get<Transform>(mirror).position.y;
    check("the server's crate fell", serverY < 4.9f);

    // The client trails the server by about the time a packet takes, which is
    // what a client is: it draws a past the server has already left. What it
    // must not do is invent a present of its own.
    check("the client is behind the server, not ahead of it", clientY >= serverY);
    // Behind by about the gap between snapshots, which is what being a client
    // costs. At 32 snapshots a second a body accelerating under gravity moves a
    // few centimetres between them.
    check("behind by about the gap between snapshots, not by a guess",
          clientY - serverY < 0.3f);
    std::printf("      server %.3f m, client %.3f m, %.0f mm apart\n",
                serverY, clientY, (clientY - serverY) * 1000.0f);

    // The unambiguous half: with the server silent, a client simulating the
    // crate would keep dropping it - and two clients dropping it from different
    // moments is the divergence a player calls "we see different worlds".
    const float before = clientWorld.get<Transform>(mirror).position.y;
    for (int i = 0; i < 60; ++i) {
        ++tick;
        client.beginTick(tick, InputCommand{}, 0);
        clientPhysics.fixedUpdate(clientCtx);
    }
    check("with nothing arriving, the client does not move it a hair",
          std::abs(clientWorld.get<Transform>(mirror).position.y - before) < 1e-6f);

    client.close();
    server.close();
}

void testAClientPredictsWhatItIsPushing() {
    std::printf("What a client is allowed to move because it is leaning on it:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    const auto addBox = [](Scene& scene, glm::vec3 at, glm::vec3 half, bool isStatic) {
        const EntityId body = scene.createEntity();
        Transform where;
        where.position = at;
        scene.add(body, where);
        Rigidbody rb;
        rb.mass     = 1.0f;
        rb.isStatic = isStatic;
        scene.add(body, rb);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = half;
        box.parts        = {part};
        scene.add(body, box);
        return body;
    };

    // A floor, a crate resting on it, and a crate far away. The player is a
    // body dropped onto the near crate.
    EntityId floorId, nearCrate, farCrate, player;
    const auto build = [&](Scene& scene) {
        floorId   = addBox(scene, {0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 40.0f}, true);
        nearCrate = addBox(scene, {0.0f,  0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, false);
        farCrate  = addBox(scene, {20.0f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, false);
        player    = addBox(scene, {0.0f,  1.6f, 0.0f}, {0.4f, 0.4f, 0.4f}, false);
        return player;
    };

    Scene serverWorld;
    build(serverWorld);
    const EntityId serverPlayer = player;
    const EntityId serverNear   = nearCrate;
    const EntityId serverFar    = farCrate;
    const EntityId serverFloor  = floorId;

    NetSession server;
    server.onSpawn([serverPlayer](Scene&, ResourceManager&, PlayerId) { return serverPlayer; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    build(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

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
    check("and comes to rest on the crate it was dropped on",
          pumpUntil(frame, [&]() { return client.leaseCount() > 0; }, 200));

    const EntityId clientNear  = clientWorld.entityAt(serverNear.slot());
    const EntityId clientFar   = clientWorld.entityAt(serverFar.slot());
    const EntityId clientFloor = clientWorld.entityAt(serverFloor.slot());

    check("the crate it is standing on is now its to move", client.simulates(clientNear));
    check("but it is still not the client's own entity", !client.isMine(clientNear));
    check("the floor is not: the closure stops at what cannot move",
          !client.simulates(clientFloor));
    check("and neither is a crate on the other side of the level",
          !client.simulates(clientFar));
    std::printf("      leasing %zu body(s) of %zu in the world\n",
                client.leaseCount(), clientWorld.entityCount());

    // Take the contact away. The lease must lapse - a crate touched once is not
    // the client's for the rest of the match.
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
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 60);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();

    Scene client;

    // Seeded rather than random, so a failure is a failure every run and not a
    // story about a seed. The point is not which packets are lost but that the
    // design does not care which.
    Math::Rng noise(12345u);
    const auto roll = [&noise](int upTo) { return noise.nextInt(0, upTo - 1); };

    // Snapshots written but not yet delivered, so they can arrive late, twice,
    // or never - which is what a network does and what the acknowledgement
    // design has to survive.
    struct InFlight {
        uint16_t             sequence = 0;
        std::vector<uint8_t> body;
    };
    std::vector<InFlight> wire;

    uint16_t sequence = 1;
    int sent = 0, lost = 0, delivered = 0, duplicated = 0;

    for (int round = 0; round < 400; ++round) {
        // The world keeps moving for the first half, then settles - which is
        // when a frozen body would show up, because nothing later would correct
        // it.
        if (round < 200) {
            for (size_t i = 0; i < crates.size(); ++i) {
                if (roll(3) != 0) continue;
                server.get<Transform>(crates[i]).position +=
                    glm::vec3(0.05f, 0.0f, 0.02f);
            }
        }

        std::vector<uint8_t> body;
        writeSnapshot(server, schema, sequence, budget, baseline, body);
        wire.push_back({sequence, body});
        ++sequence;
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

            readSnapshotFrom(client, schema, wire[i].body);
            baseline.confirm(wire[i].sequence);
            ++delivered;

            if (die > 95) {                       // and again, as a duplicate
                readSnapshotFrom(client, schema, wire[i].body);
                baseline.confirm(wire[i].sequence);
                ++duplicated;
            }
            wire.erase(wire.begin() + static_cast<long>(i));
        }
    }

    // Drain whatever is still in flight, then let it settle.
    for (int round = 0; round < 40; ++round) {
        std::vector<uint8_t> body;
        writeSnapshot(server, schema, sequence, budget, baseline, body);
        readSnapshotFrom(client, schema, body);
        baseline.confirm(sequence);
        ++sequence;
    }

    check("packets really were lost", lost > sent / 5);
    check("and some arrived twice", duplicated > 0);

    // The property that matters: after all that, the two worlds are the same.
    // Not close - the same, to the millimetre the wire carries.
    float worst = 0.0f;
    int missing = 0;
    for (EntityId crate : crates) {
        if (!client.isAliveAtIndex(crate.slot())) { ++missing; continue; }
        const glm::vec3 there = client.get<Transform>(client.entityAt(crate.slot())).position;
        const glm::vec3 here  = server.get<Transform>(crate).position;
        worst = std::max(worst, glm::length(there - here));
    }
    check("every body is on the client", missing == 0);
    check("and every one of them is where the server has it", worst < 0.002f);
    std::printf("      %d sent, %d lost, %d delivered, %d duplicated; worst body off by %.4f mm\n",
                sent, lost, delivered, duplicated, static_cast<double>(worst) * 1000.0);

    // And it did not do it by shouting: a settled world under loss still goes
    // quiet, because presence is measured against what was confirmed.
    std::vector<uint8_t> body;
    const NetSnapshotStats quiet = writeSnapshot(server, schema, sequence, budget, baseline, body);
    check("a settled world is quiet even after all that",
          quiet.entitiesWritten <= 1);
}

} // namespace

void runNetSessionTests() {
    testTwoSessionsPlayTheSameGame();
    testAClientBuiltFromDifferentSourceIsTurnedAway();
    testAFullServerSaysSoRatherThanIgnoring();
    testAClientThatLosesItsServerDoesNotInheritTheWorld();
    testAStutterDoesNotCostTheTicksItRan();
    testAServerThatHasBeenUpAWhileStillTakesAJoin();
    testStandingIsToldRatherThanGuessed();
    testAClientDoesNotMoveWhatItDoesNotOwn();
    testAClientPredictsWhatItIsPushing();
    testAWorldConvergesThroughALossyLink();
}
