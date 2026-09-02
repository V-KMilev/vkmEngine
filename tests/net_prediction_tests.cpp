#include "support.h"

namespace {

void testAJumpSurvivesTheNetworkLosingIt() {
    std::printf("An axis recovers on its own; an edge does not:\n");

    // The player jumps on tick 100 and keeps walking. Every packet after it
    // repeats that command, so the jump has twelve chances to arrive.
    std::vector<InputCommand> sent;
    for (uint32_t i = 0; i < 20; ++i) {
        sent.push_back(walkCommand(i, 100 + i, 1.0f, i == 0));
    }

    NetCommandBuffer server;
    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;

    // Drop the first eight packets outright - a burst of loss covering the
    // packet the jump was first sent in and seven after it.
    int delivered = 0;
    for (size_t frame = 1; frame <= sent.size(); ++frame) {
        const size_t oldest = frame > NET_COMMAND_REDUNDANCY
                            ? frame - NET_COMMAND_REDUNDANCY : 0;
        const std::vector<InputCommand> window(
            sent.begin() + static_cast<long>(oldest),
            sent.begin() + static_cast<long>(frame));

        BitWriter writer(packet, 512);
        writeCommands(writer, window, TEST_ACTIONS);
        writer.finish();
        if (frame <= 8) continue;

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, TEST_ACTIONS, got));
        for (const InputCommand& command : got) server.accept(command);
        ++delivered;
    }
    check("packets did arrive eventually", delivered == 12);

    // The server now runs the ticks in order, as it does in the frame loop.
    // The jump has to be in tick 100 - it was pressed, and the player is owed
    // it - and it has to be in exactly one tick. Every one of those twelve
    // packets carried a copy, so a server that ran what arrived rather than
    // what it had not yet run would jump twelve times from one keypress.
    int jumps = 0;
    InputCommand ran;
    for (uint32_t tick = 100; tick < 120; ++tick) {
        const InputCommand command = server.take(tick);
        if (tick == 100) ran = command;
        if (command.pressed & (1u << 2)) ++jumps;
    }
    check("the jump pressed eight packets ago is still there", ran.pressed == (1u << 2));
    check("on the tick it was pressed on", ran.tick == 100);
    check("with the axis it was pressed with", std::abs(ran.axis[0] - 1.0f) < 0.01f);
    check("and once, not once per copy that arrived", jumps == 1);
}

void testATickWithNoCommandStillRuns() {
    std::printf("What the server does when the next command has not arrived:\n");

    NetCommandBuffer server;
    server.accept(walkCommand(1, 50, 1.0f, true));

    const InputCommand first = server.take(50);
    check("the command runs on the tick it names", first.tick == 50);
    check("with its edge", first.pressed != 0);

    // Tick 51's command is still in flight. The server cannot wait for it.
    const InputCommand gap = server.take(51);
    check("the next tick runs anyway", gap.tick == 51);
    check("holding the axis, because the key is still held",
          std::abs(gap.axis[0] - 1.0f) < 0.01f);
    check("but not the edge, which already fired", gap.pressed == 0);

    const InputCommand secondGap = server.take(52);
    check("and it does not fire on the tick after that either", secondGap.pressed == 0);
}

void testCommandsRunInTheOrderTheyWereMade() {
    std::printf("One command, one tick, in order, once:\n");

    NetCommandBuffer server;

    // Three commands arrive together, as they do inside one packet.
    server.accept(walkCommand(1, 200, 0.5f, false));
    server.accept(walkCommand(2, 201, 0.6f, false));
    server.accept(walkCommand(3, 202, 0.7f, false));
    check("all three are waiting", server.pending() == 3);

    // One per tick, oldest first. The client predicted each of its ticks from
    // one of these in this order, so the server must run them the same way or
    // the two ends compute different answers from the same input.
    check("the first tick runs the first command",
          std::abs(server.take(900).axis[0] - 0.5f) < 0.01f);
    check("the next runs the next",
          std::abs(server.take(901).axis[0] - 0.6f) < 0.01f);
    check("and the next the next",
          std::abs(server.take(902).axis[0] - 0.7f) < 0.01f);
    check("with nothing left waiting", server.pending() == 0);

    // The sender's own numbering is kept, because that is what a snapshot hands
    // back so the client knows which prediction has been judged.
    check("the sender's number for what ran is remembered",
          server.newestRunTick() == 202);

    // Nothing waiting, and the tick has to run anyway. The command carries the
    // tick it is running on, not the one the sender was on: a system reading it
    // is in the server's tick, not the client's.
    check("a command runs stamped with the tick it is running on",
          server.take(903).tick == 903);

    // A tick moved on repeated input, so a moment of the client's was lived
    // without its say. Claiming it keeps the snapshot honest: what it confirms
    // describes the pose it carries, not one a tick further on.
    check("and a repeat claims the moment it stood in for",
          server.newestRunTick() == 203);

    // A late copy of a command already run. Running it again would run the
    // player's input twice from one keypress.
    server.accept(walkCommand(1, 200, 0.5f, false));
    check("a copy of a command already run is not queued again",
          server.pending() == 0);

    // And the real command for the moment a repeat covered. Late rather than
    // duplicate - it was never run - but the moment is spent either way, and
    // running it now would move the character twice for one moment of input.
    server.accept(walkCommand(4, 203, 0.9f, true));
    check("nor is the command a repeat already stood in for",
          server.pending() == 0);

    // Its edges survive it, though. An axis says where the stick is now and the
    // next command says it again; a press is true once, and dropping it is a
    // jump the player asked for and never got.
    server.accept(walkCommand(5, 204, 0.9f, false));
    check("but the press it carried is folded into the next that runs",
          server.take(904).pressed != 0);
}

void testWhatAFrameOfInputCostsOnTheWire() {
    std::printf("What input costs, with every copy of itself it carries:\n");

    std::vector<InputCommand> window;
    for (uint32_t i = 0; i < NET_COMMAND_REDUNDANCY; ++i) {
        window.push_back(walkCommand(i, 1000 + i, i % 2 ? 1.0f : -1.0f, i == 3));
    }

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, TEST_ACTIONS);
    writer.finish();

    std::vector<InputCommand> got;
    BitReader reader(packet.data(), packet.size());
    check("a full packet of commands decodes", readCommands(reader, TEST_ACTIONS, got));
    check("all of them", got.size() == NET_COMMAND_REDUNDANCY);
    check("and the read landed exactly", !reader.failed());

    bool same = true;
    for (size_t i = 0; i < got.size(); ++i) {
        same = same && got[i].sequence == window[i].sequence
                    && got[i].tick == window[i].tick
                    && got[i].pressed == window[i].pressed
                    && std::abs(got[i].axis[0] - window[i].axis[0]) < 0.01f
                    && rotationErrorDegrees(window[i].view, got[i].view) < 0.3f;
    }
    check("each one carrying what it was given", same);

    std::printf("      %zu commands, %zu bits each, %zu bytes a packet, %.1f KB/s up\n",
                window.size(), writer.bitCount() / window.size(), packet.size(),
                packet.size() * NET_COMMAND_RATE / 1024.0);

    // What matters is not how small it is but that a whole frame of input, with
    // every copy it carries, never approaches a datagram - else the redundancy
    // protecting a jump would be what lost the packet carrying it.
    check("a frame of input is nowhere near a datagram",
          packet.size() < UdpSocket::MAX_DATAGRAM / 3);
}

void testAPacketCannotTalkTheDecoderPastTheEndOfACommand() {
    std::printf("What a peer claiming more actions than exist is told:\n");

    // A command holds a fixed number of action slots. The count is a byte on
    // the wire, so a peer can claim 255 of them - by being a different build,
    // by being corrupted in flight, or on purpose. The decoder indexes the
    // array with it, so believing it writes off the end of the command.
    std::vector<InputCommand> window;
    window.push_back(walkCommand(1, 100, 1.0f, true));

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, MAX_INPUT_ACTIONS + 1);
    writer.finish();
    check("the encoder refuses more actions than a command holds",
          writer.bitCount() == 0);

    // And the decoder refuses independently, because it is the side that would
    // do the writing and the only one that knows what the array can hold.
    BitWriter honest(packet, 512);
    writeCommands(honest, window, MAX_INPUT_ACTIONS);
    honest.finish();

    std::vector<InputCommand> got;
    BitReader hostile(packet.data(), packet.size());
    check("and the decoder refuses to read that many",
          !readCommands(hostile, MAX_INPUT_ACTIONS + 1, got));
    check("without having written anything first", got.empty());

    // The legal maximum still works, so the refusal is a bound and not an
    // off-by-one that quietly costs a project its last action.
    BitReader legal(packet.data(), packet.size());
    check("while every slot a command really has is still usable",
          readCommands(legal, MAX_INPUT_ACTIONS, got) && got.size() == 1);
}

void testTheWorldIsDrawnSmoothlyBetweenWhatArrives() {
    std::printf("Sixty snapshots a second, drawn at a hundred and forty-four:\n");

    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    scene.add(crate, at);

    // A body sliding along x at one metre a tick, described once every four
    // ticks - a snapshot rate below the tick rate, which is the normal case -
    // while frames run at 144 a second against a 60 Hz simulation.
    const float tickRate  = 60.0f;
    const float frameTime = 1.0f / 144.0f;

    float simulated = 0.0f;   ///< Where the server's clock has got to.
    uint32_t nextSnapshot = 0;
    float previous = -1.0f;
    int forward = 0, frames = 0, stepped = 0;

    for (int i = 0; i < 300; ++i) {
        simulated += frameTime * tickRate;
        while (static_cast<float>(nextSnapshot) <= simulated) {
            smoothing.record(crate, nextSnapshot,
                             {static_cast<float>(nextSnapshot), 0.0f, 0.0f},
                             glm::angleAxis(glm::radians(static_cast<float>(nextSnapshot)),
                                            Math::WORLD_UP));
            nextSnapshot += 4;
        }

        smoothing.apply(scene, frameTime, tickRate, 32.0f);
        const float x = scene.get<Transform>(crate).position.x;
        if (i > 20) {
            if (x > previous + 1e-6f) ++forward;
            else                      ++stepped;
            ++frames;
        }
        previous = x;
    }
    check("it is tracking the body", smoothing.tracked() == 1);
    check("every frame moved it, not one frame in four", forward > frames * 9 / 10);

    // And it draws the past on purpose. Drawing the newest sample is what makes
    // a body step; drawing a moment with data on both sides of it is what makes
    // it slide.
    const float newest = static_cast<float>(nextSnapshot - 4);
    check("it is drawing behind the newest thing it heard",
          smoothing.renderTick() < newest);
    check("but not far behind it", newest - smoothing.renderTick() < 12.0f);
    std::printf("      newest news tick %.0f, drawing tick %.1f, %d of %d frames moved\n",
                newest, smoothing.renderTick(), forward, frames);
    (void)stepped;

    // With nothing more arriving it holds rather than guesses. A body that
    // overshoots and is pulled back reads worse than one that pauses for the
    // length of a lost packet.
    for (int i = 0; i < 400; ++i) smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check("and never runs past the last thing it was told",
          scene.get<Transform>(crate).position.x <= newest + 1e-4f);

    // A sample older than one already held is dropped: the newer one already
    // describes a moment past it, and inserting it would put the history out of
    // the order the search relies on.
    smoothing.record(crate, 8, {-500.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check("a sample that arrived late and stale is ignored",
          scene.get<Transform>(crate).position.x > 0.0f);
}

void testSmoothingNeverFeedsItselfItsOwnGuess() {
    std::printf("The error that compounds until a still body drifts away:\n");

    // Presence in a snapshot is measured against what the receiver was last
    // told. So if the smoothed value - which is deliberately a moment behind -
    // is sitting in the component when the next snapshot lands, "unchanged"
    // silently means "unchanged from the smoothed value", and the body walks.
    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    at.position = {10.0f, 0.0f, 0.0f};
    scene.add(crate, at);

    smoothing.record(crate, 10, {10.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.record(crate, 20, {20.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));

    // Draw a few frames: the component now holds something between the two.
    for (int i = 0; i < 5; ++i) smoothing.apply(scene, 1.0f / 60.0f, 60.0f, 32.0f);
    const float drawn = scene.get<Transform>(crate).position.x;
    check("the drawn position is behind the newest", drawn < 20.0f);

    // A snapshot arrives saying nothing about this body, so the reader leaves
    // the component alone. Restoring first is what makes "leaves it alone" mean
    // what the server said rather than what was drawn.
    smoothing.restoreConfirmed(scene);
    check("the server's last word is put back before a snapshot is read",
          std::abs(scene.get<Transform>(crate).position.x - 20.0f) < 1e-6f);
}

void testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith() {
    std::printf("And what happens when the client really was wrong:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);
    NetSession server;
    server.onSpawn([walker](Scene&, ResourceManager&, PlayerId) { return walker; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // The client believes it may walk. The server holds it against a wall the
    // client does not know about, so every tick the client predicts is wrong by
    // one more step - which is exactly the case a nudge closes slowly and a
    // replay closes at once.
    bool pushing = false;
    const auto stepClient = [&](const InputCommand& command) {
        if (!clientWorld.isAlive(clientWalker)) return;
        clientWorld.get<Transform>(clientWalker).position.x += command.axis[0] * 0.1f;
    };

    uint32_t tick = 0;
    int replayed = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);

        for (const InputCommand& again : client.replayCommands()) {
            client.beginReplayTick(again);
            stepClient(again);
            client.endTick(clientWorld, again.tick);
            ++replayed;
        }
        client.endReplay(clientWorld);

        ++tick;
        InputCommand walking;
        walking.sequence = tick;
        walking.tick     = tick;
        walking.axis[0]  = pushing ? 1.0f : 0.0f;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walking, 3);

        // The wall: whatever the client sends, the server does not move it.
        if (serverWorld.isAlive(walker)) serverWorld.get<Transform>(walker).position.x = 0.0f;
        stepClient(client.commandFor(clientWalker));

        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));

    pushing = true;
    for (int i = 0; i < 15; ++i) frame();
    const float wandered = clientWorld.get<Transform>(clientWalker).position.x;
    check("the client walked into a wall it could not see", wandered > 0.05f);
    check("was told its answer was wrong", client.predictionError() > 0.0f);
    check("and re-ran the ticks rather than arguing with the answer", replayed > 0);

    // It stops pushing. Every tick since the disagreement was recomputed from
    // the server's own state, so the next snapshot to arrive lands it exactly -
    // not a fifth of the way, and not over the following second.
    pushing = false;
    const int before = replayed;
    for (int i = 0; i < 6; ++i) frame();

    const float settled = clientWorld.get<Transform>(clientWalker).position.x;
    check("and lands on the server's answer, not near it",
          std::abs(settled) < 0.001f);
    std::printf("      wandered %.3f m, %d tick(s) re-run, settled at %.5f\n",
                static_cast<double>(wandered), replayed, static_cast<double>(settled));
    (void)before;

    client.close();
    server.close();
}

void testAShotIsJudgedAgainstWhatTheShooterCouldSee() {
    std::printf("Aiming at where somebody was, which is all anyone can do:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    // Two players: one that shoots and one that runs. Only players are rewound,
    // so a target has to be one - a crate would be judged against the present.
    Scene serverWorld;
    std::vector<EntityId> seats;
    const auto addPlayer = [&](Scene& scene) {
        const EntityId body = scene.createEntity();
        scene.add(body, Transform{});
        Rigidbody rb;
        rb.isKinematic = true;
        scene.add(body, rb);
        Collider box;
        ColliderPart part;
        part.shape       = ColliderShape::Box;
        part.halfExtents = {0.5f, 0.5f, 0.5f};
        box.parts        = {part};
        scene.add(body, box);
        return body;
    };
    seats.push_back(addPlayer(serverWorld));   // the shooter
    seats.push_back(addPlayer(serverWorld));   // the runner
    serverWorld.get<Transform>(seats[0]).position = {0.0f, 0.0f, -30.0f};

    size_t handedOut = 0;
    NetSession server;
    server.onSpawn([&](Scene&, ResourceManager&, PlayerId) {
                       return handedOut < seats.size() ? seats[handedOut++] : EntityId{};
                   },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 4);
    const uint16_t port = server.localAddress().port;

    Scene shooterWorld, runnerWorld;
    NetSession shooterClient, runnerClient;
    shooterClient.connect(NetAddress{0x7F000001u, port});
    runnerClient.connect(NetAddress{0x7F000001u, port});

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        shooterClient.receive(shooterWorld, resources);
        runnerClient.receive(runnerWorld, resources);

        // What Engine::run does every frame, and what advances the clock that
        // decides which moment a client is looking at - which is the number the
        // whole feature is built on.
        shooterClient.interpolate(shooterWorld, 1.0f / 60.0f, 60.0f);
        runnerClient.interpolate(runnerWorld, 1.0f / 60.0f, 60.0f);

        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        shooterClient.beginTick(tick, InputCommand{}, 3);
        runnerClient.beginTick(tick, InputCommand{}, 3);

        // The runner moves a metre a tick, so where it was N ticks ago is a
        // number rather than an estimate.
        if (handedOut > 1) {
            serverWorld.get<Transform>(seats[1]).position.x = static_cast<float>(tick);
        }

        server.endTick(serverWorld, tick);
        shooterClient.endTick(shooterWorld, tick);
        runnerClient.endTick(runnerWorld, tick);
        server.send(serverWorld, tick);
        shooterClient.send(shooterWorld, tick);
        runnerClient.send(runnerWorld, tick);
        server.advance(1.0f / 60.0f);
        shooterClient.advance(1.0f / 60.0f);
        runnerClient.advance(1.0f / 60.0f);
    };

    check("both players join",
          pumpUntil(frame, [&]() { return shooterClient.isPlaying() && runnerClient.isPlaying(); }, 80));
    const EntityId shooter = seats[0];
    check("and were given different characters",
          shooterClient.localEntity().slot() != runnerClient.localEntity().slot());

    for (int i = 0; i < 60; ++i) frame();

    const float now = serverWorld.get<Transform>(seats[1]).position.x;
    check("the runner has covered ground", now > 30.0f);

    const auto hits = [&](float x, bool rewound) {
        RayHit hit;
        QueryFilter filter;
        filter.ignore = shooter;
        if (!rewound) {
            return raycast(serverWorld, {x, 0.0f, -10.0f}, {0.0f, 0.0f, 1.0f}, 20.0f, hit, filter);
        }
        NetRewindScope scope(serverWorld, server, shooter);
        return raycast(serverWorld, {x, 0.0f, -10.0f}, {0.0f, 0.0f, 1.0f}, 20.0f, hit, filter);
    };

    check("a shot at where the runner is right now hits, judged live",
          hits(now, false));

    // The same shot judged against the present MISSES, because the runner was
    // not there when the player who fired could see anything. That is the
    // feature: aiming at the present is aiming where nobody can see.
    check("  and misses once the world is put back to the shooter's view",
          !hits(now, true));

    // The point of the whole feature: the shooter's screen is showing it a
    // moment already past, so a shot aimed there must be judged against that
    // moment rather than against the present.
    // Where the server puts the runner when it judges this player's shot -
    // measured rather than guessed at, so the test says something exact.
    float seenAt = 0.0f;
    {
        NetRewindScope scope(serverWorld, server, shooter);
        seenAt = serverWorld.get<Transform>(seats[1]).position.x;
    }
    check("the server puts the runner back to an earlier moment", seenAt < now - 0.5f);
    check("a shot aimed there would have missed the present", !hits(seenAt, false));
    check("  and lands when judged against what the shooter could see",
          hits(seenAt, true));
    std::printf("      runner now at %.1f, shooter was seeing %.1f - %.1f ticks back\n",
                static_cast<double>(now), static_cast<double>(seenAt),
                static_cast<double>(now - seenAt));

    // And nothing outside the scope can tell that the world moved.
    const glm::vec3 before = serverWorld.get<Transform>(seats[1]).position;
    { NetRewindScope scope(serverWorld, server, shooter); }
    check("the present is put back exactly",
          serverWorld.get<Transform>(seats[1]).position == before);

    shooterClient.close();
    runnerClient.close();
    server.close();
}

void testAPlayerIsNotDraggedBackwardsByTheirOwnConnection() {
    std::printf("The one thing a player always notices:\n");

    const ScopedEngineSchema wire;

    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);

    NetSession server;
    server.onSpawn([walker](Scene&, ResourceManager&, PlayerId) { return walker; },
                   [](Scene&, ResourceManager&, PlayerId, EntityId) {});
    server.host(0, 2);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port});

    // One rule, run by both ends: step along x by whatever the command says.
    // That is what prediction is - the client running the rule the server will
    // run, on its own input, without waiting to be told the answer.
    const auto step = [](Scene& scene, NetSession& net, EntityId entity) {
        if (!net.isPlaying() || !scene.isAlive(entity)) return;
        scene.get<Transform>(entity).position.x += net.commandFor(entity).axis[0];
    };

    uint32_t tick = 0;
    int replayed = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);

        // What Engine::run does between receiving and this frame's own ticks:
        // every tick the server disagreed about is run again from the server's
        // answer, under the command it originally ran.
        for (const InputCommand& again : client.replayCommands()) {
            client.beginReplayTick(again);
            step(clientWorld, client, clientWalker);
            client.endTick(clientWorld, again.tick);
            ++replayed;
        }
        client.endReplay(clientWorld);

        ++tick;

        InputCommand walking;
        walking.sequence = tick;
        walking.tick     = tick;
        walking.axis[0]  = 1.0f;      // held forward, every tick
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, walking, 3);

        step(serverWorld, server, walker);
        step(clientWorld, client, clientWalker);

        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(0.016f);
        client.advance(0.016f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and drives the character", client.localEntity().slot() == walker.slot());

    // Let the two settle into step first. The very first snapshot after a join
    // is compared against a prediction made before the server had any of this
    // client's input, so one correction there is the mechanism working.
    for (int i = 0; i < 20; ++i) frame();
    replayed = 0;
    for (int i = 0; i < 60; ++i) frame();

    const float clientX = clientWorld.get<Transform>(clientWalker).position.x;
    const float serverX = serverWorld.get<Transform>(walker).position.x;

    // The server is behind by the commands still in flight and always will be.
    // A client that believed each snapshot would jump its character back by
    // that much on every packet, which reads as broken rather than as lag.
    check("the server is describing a moment the client has passed", serverX < clientX);
    check("and the client's own character is still out in front",
          clientX > serverX + 0.5f);

    // And the two ran the same rule on the same input, so once the server
    // catches up to a tick the client predicted, they agree exactly. That is
    // the number that decides whether the character feels solid.
    check("their answers for the same tick agree", client.predictionError() < 0.01f);
    check("so once they are in step, nothing has to be run again at all",
          replayed == 0);
    std::printf("      client at %.1f, server at %.1f, prediction out by %.4f m\n",
                clientX, serverX, static_cast<double>(client.predictionError()));

    client.close();
    server.close();
}

} // namespace

void runNetPredictionTests() {
    testAJumpSurvivesTheNetworkLosingIt();
    testATickWithNoCommandStillRuns();
    testCommandsRunInTheOrderTheyWereMade();
    testWhatAFrameOfInputCostsOnTheWire();
    testAPacketCannotTalkTheDecoderPastTheEndOfACommand();
    testTheWorldIsDrawnSmoothlyBetweenWhatArrives();
    testSmoothingNeverFeedsItselfItsOwnGuess();
    testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith();
    testAShotIsJudgedAgainstWhatTheShooterCouldSee();
    testAPlayerIsNotDraggedBackwardsByTheirOwnConnection();
}
