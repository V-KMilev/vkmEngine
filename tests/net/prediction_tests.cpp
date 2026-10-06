#include "net/net_support.h"

#include <map>

#include "core/math/random.h"
#include "ecs/component/physics/collider.h"
#include "system/physics/query/query.h"

namespace {

void testAJumpSurvivesTheNetworkLosingIt() {
    std::printf("An axis recovers on its own; an edge does not:\n");

    // The player jumps on tick 100 and keeps walking; the next eleven packets repeat
    // that command, so the jump has twelve chances to arrive.
    std::vector<InputCommand> sent;
    for (uint32_t i = 0; i < 20; ++i) {
        sent.push_back(walkCommand(i, 100 + i, 1.0f, i == 0));
    }

    NetCommandBuffer server;
    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;

    // Drop the first eight packets - a loss burst covering the jump's packet and seven after.
    int delivered = 0;
    for (size_t frame = 1; frame <= sent.size(); ++frame) {
        const size_t oldest = frame > NET_COMMAND_REDUNDANCY ? frame - NET_COMMAND_REDUNDANCY : 0;
        const std::vector<InputCommand> window(
            sent.begin() + static_cast<long>(oldest),
            sent.begin() + static_cast<long>(frame)
        );

        BitWriter writer(packet, 512);
        writeCommands(writer, window, 0, TEST_ACTIONS);
        writer.finish();
        if (frame <= 8) continue;

        BitReader reader(packet.data(), packet.size());
        check("the packet decodes", readCommands(reader, TEST_ACTIONS, got));
        for (const InputCommand& command : got) server.accept(command);
        ++delivered;
    }
    check("packets did arrive eventually", delivered == 12);

    // The server runs the ticks in order. The jump belongs to exactly one tick: four
    // arriving packets carried a copy, and running each would jump four times.
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

    // Tick 51's command is still in flight; the server cannot wait for it.
    const InputCommand gap = server.take(51);
    check("the next tick runs anyway", gap.tick == 51);
    check("holding the axis, because the key is still held", std::abs(gap.axis[0] - 1.0f) < 0.01f);
    check("but not the edge, which already fired", gap.pressed == 0);

    const InputCommand secondGap = server.take(52);
    check("and it does not fire on the tick after that either", secondGap.pressed == 0);
}

// Holding the last command is a guess about a player still there. Past
// NetCommandBuffer::REPEAT_TICKS the axes are zeroed, so a dead link leaves the
// character standing rather than walking off on the last thing it was told.
void testALinkThatHasGoneStopsTheBodyRatherThanWalkingIt() {
    std::printf("A repeat is a guess, and it stops being one worth making:\n");

    NetCommandBuffer server;
    server.accept(walkCommand(1, 50, 1.0f, false));
    check("the command runs", std::abs(server.take(50).axis[0] - 1.0f) < 0.01f);

    // Every dry tick up to the boundary still walks: the clamp is strictly greater, so
    // the last held tick is 50 + REPEAT_TICKS.
    bool heldThroughout = true;
    for (uint32_t tick = 51; tick <= 50 + NetCommandBuffer::REPEAT_TICKS; ++tick) {
        heldThroughout = heldThroughout && std::abs(server.take(tick).axis[0] - 1.0f) < 0.01f;
    }
    check("  and every dry tick inside the repeat window holds it", heldThroughout);

    const InputCommand past = server.take(51 + NetCommandBuffer::REPEAT_TICKS);
    check("  the first tick past the window zeroes the axis", std::abs(past.axis[0]) < 0.01f);
    check(
        "  and the one after it stays zeroed",
        std::abs(server.take(52 + NetCommandBuffer::REPEAT_TICKS).axis[0]) < 0.01f
    );

    // A command arriving puts it back: the clamp is a silence, not a state.
    server.accept(walkCommand(2, 60 + NetCommandBuffer::REPEAT_TICKS, -1.0f, false));
    check(
        "and a command that does arrive is obeyed again",
        std::abs(server.take(60 + NetCommandBuffer::REPEAT_TICKS).axis[0] + 1.0f) < 0.01f
    );
}

void testCommandsRunInTheOrderTheyWereMade() {
    std::printf("One command, one tick, in order, once:\n");

    NetCommandBuffer server;

    // Three commands arrive together, as inside one packet.
    server.accept(walkCommand(1, 200, 0.5f, false));
    server.accept(walkCommand(2, 201, 0.6f, false));
    server.accept(walkCommand(3, 202, 0.7f, false));
    check("all three are waiting", server.pending() == 3);

    // One per tick, oldest first: the client predicted its ticks in this order, so the
    // server must run them so, or the two ends compute different answers.
    check("the first tick runs the first command", std::abs(server.take(900).axis[0] - 0.5f) < 0.01f);
    check("the next runs the next", std::abs(server.take(901).axis[0] - 0.6f) < 0.01f);
    check("and the next the next", std::abs(server.take(902).axis[0] - 0.7f) < 0.01f);
    check("with nothing left waiting", server.pending() == 0);

    // The sender's numbering is kept: a snapshot hands it back so the client knows which
    // prediction was judged.
    check("the sender's number for what ran is remembered", server.newestRunTick() == 202);

    // Nothing waiting, and the tick runs anyway. The command is stamped with the
    // server's tick, which is where a system reading it is.
    check("a command runs stamped with the tick it is running on", server.take(903).tick == 903);

    // A tick moved on repeated input lived a client moment without its say. Claiming it
    // keeps the snapshot honest: it confirms the pose it carries, not a tick further on.
    check("and a repeat claims the moment it stood in for", server.newestRunTick() == 203);

    // A late copy of a command already run: running it again doubles one keypress.
    server.accept(walkCommand(1, 200, 0.5f, false));
    check("a copy of a command already run is not queued again", server.pending() == 0);

    // The real command for a moment a repeat covered: late, not duplicate, but the moment
    // is spent, and running it would move the character twice for one moment.
    server.accept(walkCommand(4, 203, 0.9f, true));
    check("nor is the command a repeat already stood in for", server.pending() == 0);

    // Its edges survive, though: an axis is restated by the next command, but a press is
    // true once, and dropping it is a jump asked for and never got.
    server.accept(walkCommand(5, 204, 0.9f, false));
    check("but the press it carried is folded into the next that runs", server.take(904).pressed != 0);
}

// A packet numbers each command one on from the last, so a run with a hole cannot be
// one run: everything after would decode under the wrong tick, which a snapshot hands
// back to be judged.
void testACommandIsNeverNumberedAcrossAGap() {
    std::printf("Commands either side of a hole in the numbering:\n");

    std::vector<InputCommand> window;
    for (uint32_t i = 0; i < 3; ++i) window.push_back(walkCommand(1 + i, 500 + i, 1.0f, false));
    for (uint32_t i = 0; i < 2; ++i) window.push_back(walkCommand(104 + i, 603 + i, 1.0f, false));

    std::vector<uint8_t> packet;
    std::vector<InputCommand> got;

    BitWriter first(packet, 512);
    check("a packet stops at the hole", writeCommands(first, window, 0, TEST_ACTIONS) == 3);
    first.finish();
    BitReader firstReader(packet.data(), packet.size());
    check(
        "  and what it carries decodes",
        readCommands(firstReader, TEST_ACTIONS, got)
            && got.size() == 3
            && got.back().sequence == 3
            && got.back().tick == 502
    );

    BitWriter second(packet, 512);
    check("the next starts after it", writeCommands(second, window, 3, TEST_ACTIONS) == 2);
    second.finish();
    BitReader secondReader(packet.data(), packet.size());
    check(
        "  under the numbers they were made with",
        readCommands(secondReader, TEST_ACTIONS, got)
            && got.size() == 2
            && got.front().sequence == 104
            && got.front().tick == 603
            && got.back().sequence == 105
            && got.back().tick == 604
    );
}

// Which unconfirmed command a packet starts from. Anchored only on what comes back, the
// window would move a packet per round trip; see
// testALongRoundTripKeepsInputOneTripOld (session_tests.cpp) for the cost.
void testAPacketCarriesWhatTheServerHasNotHeard() {
    std::printf("Where a command packet starts:\n");

    std::vector<InputCommand> unconfirmed;
    for (uint32_t i = 1; i <= 20; ++i) unconfirmed.push_back(walkCommand(i, 100 + i, 1.0f, false));

    check(
        "after a stutter, everything no packet has carried, however old",
        firstCommandToSend(unconfirmed, 0, 0) == 0
    );
    check(
        "  and past what has gone, from the oldest not yet sent",
        firstCommandToSend(unconfirmed, 0, 5) == 5
    );
    check(
        "once all have gone, the newest window of them",
        firstCommandToSend(unconfirmed, 0, 20) == 20 - NET_COMMAND_REDUNDANCY
    );
    check("and nothing the server has said it has", firstCommandToSend(unconfirmed, 15, 20) == 15);
}

void testWhatAFrameOfInputCostsOnTheWire() {
    std::printf("What input costs, with every copy of itself it carries:\n");

    std::vector<InputCommand> window;
    for (uint32_t i = 0; i < NET_COMMAND_REDUNDANCY; ++i) {
        window.push_back(walkCommand(i, 1000 + i, i % 2 ? 1.0f : -1.0f, i == 3));
    }

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, 0, TEST_ACTIONS);
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

    std::printf(
        "      %zu commands, %zu bits each, %zu bytes a packet, %.1f KB/s up\n",
        window.size(),
        writer.bitCount() / window.size(),
        packet.size(),
        packet.size() * NET_COMMAND_RATE / 1024.0
    );

    // A frame of input, every copy included, must stay far from a datagram, or the
    // redundancy protecting a jump would lose the packet carrying it.
    check("a frame of input is nowhere near a datagram", packet.size() < UdpSocket::MAX_DATAGRAM / 3);
}

void testAPacketCannotTalkTheDecoderPastTheEndOfACommand() {
    std::printf("What a peer claiming more actions than exist is told:\n");

    // The action count is a byte on the wire, so a peer can claim 255; the decoder
    // indexes the fixed array with it, so believing it writes off the end.
    std::vector<InputCommand> window;
    window.push_back(walkCommand(1, 100, 1.0f, true));

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 512);
    writeCommands(writer, window, 0, MAX_INPUT_ACTIONS + 1);
    writer.finish();
    check("the encoder refuses more actions than a command holds", writer.bitCount() == 0);

    // The decoder refuses on its own: it does the writing and knows the array's size.
    BitWriter honest(packet, 512);
    writeCommands(honest, window, 0, MAX_INPUT_ACTIONS);
    honest.finish();

    std::vector<InputCommand> got;
    BitReader hostile(packet.data(), packet.size());
    check("and the decoder refuses to read that many", !readCommands(hostile, MAX_INPUT_ACTIONS + 1, got));
    check("without having written anything first", got.empty());

    // The legal maximum still works: a bound, not an off-by-one costing the last action.
    BitReader legal(packet.data(), packet.size());
    check(
        "while every slot a command really has is still usable",
        readCommands(legal, MAX_INPUT_ACTIONS, got) && got.size() == 1
    );
}

void testTheWorldIsDrawnSmoothlyBetweenWhatArrives() {
    std::printf("Fifteen snapshots a second, drawn at a hundred and forty-four:\n");

    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    scene.add(crate, at);

    // A body sliding a metre a tick, described every four ticks (snapshots slower than
    // ticks, the normal case), frames at 144 Hz against a 60 Hz simulation.
    const float tickRate  = 60.0f;
    const float frameTime = 1.0f / 144.0f;

    float simulated = 0.0f;   // Where the server's clock has got to.
    uint32_t nextSnapshot = 0;
    float previous = -1.0f;
    int forward = 0, frames = 0;

    for (int i = 0; i < 300; ++i) {
        simulated += frameTime * tickRate;
        while (static_cast<float>(nextSnapshot) <= simulated) {
            smoothing.record(
                crate,
                nextSnapshot,
                {static_cast<float>(nextSnapshot), 0.0f, 0.0f},
                glm::angleAxis(glm::radians(static_cast<float>(nextSnapshot)), Math::WORLD_UP)
            );
            nextSnapshot += 4;
        }

        smoothing.apply(scene, frameTime, tickRate, 32.0f);
        const float x = scene.get<Transform>(crate).position.x;
        if (i > 20) {
            if (x > previous + 1e-6f) ++forward;
            ++frames;
        }
        previous = x;
    }
    check("it is tracking the body", smoothing.tracked() == 1);
    check("every frame moved it, not one frame in four", forward > frames * 9 / 10);

    // It draws the past on purpose: the newest sample makes a body step; a moment with
    // data on both sides makes it slide.
    const float newest = static_cast<float>(nextSnapshot - 4);
    check("it is drawing behind the newest thing it heard", smoothing.renderTick() < newest);
    check("but not far behind it", newest - smoothing.renderTick() < 12.0f);
    std::printf(
        "      newest news tick %.0f, drawing tick %.1f, %d of %d frames moved\n",
        newest,
        smoothing.renderTick(),
        forward,
        frames
    );

    // With nothing more arriving it holds rather than guesses: an overshoot pulled back
    // reads worse than a pause the length of a lost packet.
    for (int i = 0; i < 400; ++i) smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check(
        "and never runs past the last thing it was told",
        scene.get<Transform>(crate).position.x <= newest + 1e-4f
    );

    // A sample older than one held is dropped: the newer already describes a later
    // moment, and inserting it would break the order the search relies on.
    smoothing.record(crate, 8, {-500.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.apply(scene, frameTime, tickRate, 32.0f);
    check("a sample that arrived late and stale is ignored", scene.get<Transform>(crate).position.x > 0.0f);
}

void testAServerUpForDaysIsDrawnAsSmoothlyAsANewOne() {
    std::printf("A server a day and a half into its run, drawn at a hundred and forty-four:\n");

    // The render clock counts server ticks since start. 2^24 is a day and a half at
    // 128 Hz, where a float's spacing is two ticks and a sub-tick advance is lost.
    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    scene.add(crate, Transform{});

    const float    tickRate     = 128.0f;
    const float    snapshotRate = 64.0f;
    const float    frameTime    = 1.0f / 144.0f;
    const uint32_t start        = 1u << 24;

    double   server       = 0.0;
    uint32_t nextSnapshot = 0;
    float    previous     = -1.0f;
    float    worstBehind  = 0.0f;
    int      forward = 0, frames = 0;
    for (int i = 0; i < 600; ++i) {
        server += static_cast<double>(frameTime * tickRate);
        while (static_cast<double>(nextSnapshot) <= server) {
            smoothing.record(
                crate,
                start + nextSnapshot,
                {static_cast<float>(nextSnapshot) * 0.01f, 0.0f, 0.0f},
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            );
            nextSnapshot += 2;
        }
        smoothing.apply(scene, frameTime, tickRate, snapshotRate);

        const float x = scene.get<Transform>(crate).position.x;
        if (i > 60) {
            if (x > previous + 1e-6f) ++forward;
            worstBehind = std::max(worstBehind, smoothing.behindTicks());
            ++frames;
        }
        previous = x;
    }

    const float delay = smoothing.delayTicks(tickRate, snapshotRate);
    check("every frame still moves the body", forward > frames * 9 / 10);
    check("and the clock holds its delay rather than stalling until it snaps", worstBehind < delay + 2.5f);
    std::printf(
        "      tick %u: %d of %d frames moved, at most %.2f ticks behind (the delay is %.0f)\n",
        start,
        forward,
        frames,
        worstBehind,
        delay
    );
}

/// A minute of drawing a body that moves every tick.
struct DrawnMinute {
    int   stalled = 0;      ///< Frames that drew it where the frame before had.
    int   frames  = 0;
    float delay   = 0.0f;   ///< Where the delay settled, in snapshots.
};

/**
 * @brief A body moving a metre a tick at 128 Hz, described 64 times a second across a
 *        lossy, jittery link, drawn at 144 Hz for a minute.
 *
 * Each snapshot is delayed 30 ms plus up to @p jitter. One older than one already read
 * is refused, as the connection does. Unmeasured, it is the fixed two-snapshot delay.
 *
 * @param jitter    Most seconds a snapshot is held past the 30 ms, drawn uniformly.
 * @param lossEvery Every this-many-th snapshot is lost; zero loses none.
 * @param measured  Whether the smoothing is told when each snapshot arrived.
 * @return The counted minute: frames stalled, frames counted, and the delay it settled on.
 */
DrawnMinute drawAcrossJitter(double jitter, int lossEvery, bool measured) {
    constexpr float  TICK_RATE     = 128.0f;
    constexpr float  SNAPSHOT_RATE = 64.0f;
    constexpr double FRAME         = 1.0 / 144.0;
    constexpr double LATENCY       = 0.030;
    constexpr int    SNAPSHOTS     = 64 * 70;

    struct Arrival {
        double   at   = 0.0;
        uint32_t tick = 0;
    };
    Math::Rng rng(11);
    std::vector<Arrival> arrivals;
    for (int k = 1; k <= SNAPSHOTS; ++k) {
        if (lossEvery > 0 && k % lossEvery == 0) continue;
        arrivals.push_back(
            {
                k / static_cast<double>(SNAPSHOT_RATE) + LATENCY + jitter * rng.nextFloat(),
                static_cast<uint32_t>(2 * k)
            }
        );
    }
    std::sort(
        arrivals.begin(),
        arrivals.end(),
        [](const Arrival& a, const Arrival& b) { return a.at < b.at; }
    );

    NetInterpolation smoothing;
    Scene scene;
    const EntityId body = scene.createEntity();
    scene.add(body, Transform{});

    DrawnMinute minute;
    size_t   next   = 0;
    uint32_t newest = 0;
    float    previous = 0.0f;
    for (double now = 0.0; now < SNAPSHOTS / static_cast<double>(SNAPSHOT_RATE); now += FRAME) {
        for (; next < arrivals.size() && arrivals[next].at <= now; ++next) {
            const uint32_t tick = arrivals[next].tick;
            if (tick <= newest) continue;
            newest = tick;
            if (measured) smoothing.heard(tick, TICK_RATE);
            smoothing.record(
                body,
                tick,
                {static_cast<float>(tick), 0.0f, 0.0f},
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
            );
        }
        smoothing.apply(scene, static_cast<float>(FRAME), TICK_RATE, SNAPSHOT_RATE);

        const float x = scene.get<Transform>(body).position.x;
        if (now > 10.0) {
            if (x <= previous) ++minute.stalled;
            ++minute.frames;
        }
        previous = x;
    }
    minute.delay = smoothing.delayTicks(TICK_RATE, SNAPSHOT_RATE) * SNAPSHOT_RATE / TICK_RATE;
    return minute;
}

// The delay's one job: a sample past the moment drawn. A fixed two snapshots does it
// on a steady link, even losing one in ten, but past ~30 ms of jitter a late snapshot
// runs the clock into the newest sample and the world pauses for everyone. So the
// delay is measured - and a link not needing more must not pay for it.
void testTheDelayRidesOutTheJitterItMeasures() {
    std::printf("Snapshots that arrive late, and some not at all:\n");

    const DrawnMinute clean = drawAcrossJitter(0.0, 0, true);
    check("a clean link keeps the least delay", clean.delay == NetInterpolation::SNAPSHOTS_BEHIND);
    check("  and never stalls", clean.stalled == 0);

    const DrawnMinute mild = drawAcrossJitter(0.015, 10, true);
    check(
        "fifteen milliseconds of jitter and one snapshot in ten lost needs no more",
        mild.delay == NetInterpolation::SNAPSHOTS_BEHIND && mild.stalled == 0
    );

    const DrawnMinute fixed    = drawAcrossJitter(0.045, 10, false);
    const DrawnMinute measured = drawAcrossJitter(0.045, 10, true);
    check("forty-five milliseconds stalls a fixed delay", fixed.stalled > 20);
    check("  the measured one far less often", measured.stalled * 4 <= fixed.stalled);
    check(
        "  by drawing further back, within what the samples cover",
        measured.delay > NetInterpolation::SNAPSHOTS_BEHIND
            && measured.delay <= NetInterpolation::MAX_SNAPSHOTS_BEHIND
    );
    std::printf(
        "      45 ms of jitter and one in ten lost: %d of %d frames stalled at a fixed "
        "delay, %d at %.2f snapshots measured\n",
        fixed.stalled,
        fixed.frames,
        measured.stalled,
        static_cast<double>(measured.delay)
    );
}

void testSmoothingNeverFeedsItselfItsOwnGuess() {
    std::printf("The error that compounds until a still body drifts away:\n");

    // Presence is measured against what the receiver was last told, so a smoothed value
    // (deliberately behind) left in the component makes "unchanged" relative to it, and
    // the body walks.
    NetInterpolation smoothing;
    Scene scene;
    const EntityId crate = scene.createEntity();
    Transform at;
    at.position = {10.0f, 0.0f, 0.0f};
    scene.add(crate, at);

    smoothing.record(crate, 10, {10.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));
    smoothing.record(crate, 20, {20.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0));

    // A few frames: the component now holds something between the two.
    for (int i = 0; i < 5; ++i) smoothing.apply(scene, 1.0f / 60.0f, 60.0f, 32.0f);
    const float drawn = scene.get<Transform>(crate).position.x;
    check("the drawn position is behind the newest", drawn < 20.0f);

    // A snapshot silent about this body leaves the component alone; restoring first
    // makes that mean what the server said, not what was drawn.
    smoothing.restoreConfirmed(scene);
    check(
        "the server's last word is put back before a snapshot is read",
        std::abs(scene.get<Transform>(crate).position.x - 20.0f) < 1e-6f
    );
}

void testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith() {
    std::printf("And what happens when the client really was wrong:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const EntityId walker = addWalker(serverWorld);
    NetSession server;
    server.onSpawn(
        [walker](Scene&, ResourceManager&, PlayerId) { return walker; },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    server.host(0, 2, TEST_TICK_RATE);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    // The server holds the client against a wall it does not know about, so each
    // predicted tick is one step more wrong: a nudge closes it slowly, a replay at once.
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

    // It stops pushing. Every tick since the disagreement was recomputed from the
    // server's state, so the next snapshot lands it exactly, not gradually.
    pushing = false;
    for (int i = 0; i < 6; ++i) frame();

    const float settled = clientWorld.get<Transform>(clientWalker).position.x;
    check("and lands on the server's answer, not near it", std::abs(settled) < 0.001f);
    std::printf(
        "      wandered %.3f m, %d tick(s) re-run, settled at %.5f\n",
        static_cast<double>(wandered),
        replayed,
        static_cast<double>(settled)
    );

    client.close();
    server.close();
}

void testAShotIsJudgedAgainstWhatTheShooterCouldSee() {
    std::printf("Aiming at where somebody was, which is all anyone can do:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    // A shooter and a runner. Only players are rewound, so the target must be one - a
    // crate would be judged against the present.
    Scene serverWorld;
    std::vector<EntityId> seats;
    const auto addPlayer = [&](Scene& scene) {
        const EntityId body = scene.createEntity();
        scene.add(body, Transform{});
        Rigidbody rb;
        rb.motion = RigidbodyMotion::Kinematic;
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
    server.onSpawn(
        [&](Scene&, ResourceManager&, PlayerId) {
            return handedOut < seats.size() ? seats[handedOut++] : EntityId{};
        },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    server.host(0, 4, TEST_TICK_RATE);
    const uint16_t port = server.localAddress().port;

    Scene shooterWorld, runnerWorld;
    NetSession shooterClient, runnerClient;
    shooterClient.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);
    runnerClient.connect(NetAddress{0x7F000001u, port}, TEST_TICK_RATE);

    // Where the shooter's screen showed the runner at each command's tick: what a shot
    // carried by that command aimed at.
    std::map<uint32_t, float> aimedAt;

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        shooterClient.receive(shooterWorld, resources);
        runnerClient.receive(runnerWorld, resources);

        // As Engine::run does each frame; it advances the clock deciding which moment a
        // client sees - the number the feature is built on.
        shooterClient.interpolate(shooterWorld, 1.0f / 60.0f);
        runnerClient.interpolate(runnerWorld, 1.0f / 60.0f);

        ++tick;
        if (shooterWorld.isAliveAtIndex(seats[1].slot())) {
            const EntityId seen = shooterWorld.entityAt(seats[1].slot());
            if (const Transform* at = shooterWorld.tryGet<Transform>(seen)) aimedAt[tick] = at->position.x;
        }
        server.beginTick(tick, InputCommand{}, 3);
        shooterClient.beginTick(tick, walkCommand(tick, tick, 0.0f, false), 3);
        runnerClient.beginTick(tick, InputCommand{}, 3);

        // The runner moves a metre a tick, so where it was N ticks ago is exact.
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

    check(
        "both players join",
        pumpUntil(frame, [&]() { return shooterClient.isPlaying() && runnerClient.isPlaying(); }, 80)
    );
    const EntityId shooter = seats[0];
    check(
        "and were given different characters",
        shooterClient.localEntity().slot() != runnerClient.localEntity().slot()
    );

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
        NetSession::Rewind scope(server, serverWorld, shooter);
        return raycast(serverWorld, {x, 0.0f, -10.0f}, {0.0f, 0.0f, 1.0f}, 20.0f, hit, filter);
    };

    check("a shot at where the runner is right now hits, judged live", hits(now, false));

    // Judged against the shooter's view the same shot MISSES: the runner was not there
    // in any moment the shooter could see. Aiming at the present aims where nobody sees.
    check("  and misses once the world is put back to the shooter's view", !hits(now, true));

    // The shooter's screen shows a past moment, so the shot is judged there; where the
    // server puts the runner then is measured, so this is exact.
    float seenAt = 0.0f;
    {
        NetSession::Rewind scope(server, serverWorld, shooter);
        seenAt = serverWorld.get<Transform>(seats[1]).position.x;
    }
    check("the server puts the runner back to an earlier moment", seenAt < now - 0.5f);
    check("a shot aimed there would have missed the present", !hits(seenAt, false));
    check("  and lands when judged against what the shooter could see", hits(seenAt, true));

    // The moment of the command being run, which waited behind newer ones; judged by
    // the newest, every shot lands as late as the queue is deep.
    const uint32_t running = server.commandFor(shooter).sequence;
    check("the command being run has waited behind newer ones", tick - running >= 3);
    check(
        "the runner is put where the shooter saw it when making that command",
        aimedAt.count(running) == 1 && std::abs(seenAt - aimedAt[running]) < 0.05f
    );
    std::printf(
        "      runner now at %.1f, shooter was seeing %.1f - %.1f ticks back, "
        "for a command %u ticks old\n",
        static_cast<double>(now),
        static_cast<double>(seenAt),
        static_cast<double>(now - seenAt),
        tick - running
    );

    // Nothing outside the scope can tell that the world moved.
    const glm::vec3 before = serverWorld.get<Transform>(seats[1]).position;
    { NetSession::Rewind scope(server, serverWorld, shooter); }
    check("the present is put back exactly", serverWorld.get<Transform>(seats[1]).position == before);

    // A nested scope - a helper opening its own for a shot it was handed - takes the
    // outer one's moment, and only the outer end restores the present.
    float insideInner = 0.0f;
    float afterInner  = 0.0f;
    {
        NetSession::Rewind outer(server, serverWorld, shooter);
        {
            NetSession::Rewind inner(server, serverWorld, shooter);
            insideInner = serverWorld.get<Transform>(seats[1]).position.x;
        }
        afterInner = serverWorld.get<Transform>(seats[1]).position.x;
    }
    check("a scope inside another sees the same moment", insideInner == seenAt);
    check("  closing it leaves the outer one's moment standing", afterInner == seenAt);
    check(
        "  and closing the outer one puts the present back",
        serverWorld.get<Transform>(seats[1]).position == before
    );

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
    server.onSpawn(
        [walker](Scene&, ResourceManager&, PlayerId) { return walker; },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    server.host(0, 2, TEST_TICK_RATE);

    Scene clientWorld;
    const EntityId clientWalker = addWalker(clientWorld);
    NetSession client;
    client.connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);

    // One rule, run by both ends: step along x by the command. Prediction is the client
    // running the server's rule on its own input without waiting for the answer.
    const auto step = [](Scene& scene, NetSession& net, EntityId entity) {
        if (!net.isPlaying() || !scene.isAlive(entity)) return;
        scene.get<Transform>(entity).position.x += net.commandFor(entity).axis[0];
    };

    uint32_t tick = 0;
    int replayed = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);

        // As Engine::run does before this frame's ticks: every disputed tick runs again
        // from the server's answer, under the command it originally ran.
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

    // Let the two settle first: the first snapshot after a join is compared against a
    // prediction made before the server had any input, so one correction is expected.
    for (int i = 0; i < 20; ++i) frame();
    replayed = 0;
    for (int i = 0; i < 60; ++i) frame();

    const float clientX = clientWorld.get<Transform>(clientWalker).position.x;
    const float serverX = serverWorld.get<Transform>(walker).position.x;

    // The server trails by the commands in flight, always. A client believing each
    // snapshot would jump back that far every packet, reading as broken, not lag.
    check("the server is describing a moment the client has passed", serverX < clientX);
    check("and the client's own character is still out in front", clientX > serverX + 0.5f);

    // Same rule, same input: once the server reaches a predicted tick they agree
    // exactly - what decides whether the character feels solid.
    check("their answers for the same tick agree", client.predictionError() < 0.01f);
    check("so once they are in step, nothing has to be run again at all", replayed == 0);
    std::printf(
        "      client at %.1f, server at %.1f, prediction out by %.4f m\n",
        clientX,
        serverX,
        static_cast<double>(client.predictionError())
    );

    client.close();
    server.close();
}

// Pacing is steered by a number the server sends, so it must hold whatever it says: a
// server reporting an always-empty or bottomless queue bends ticks by the bound only.
void testPacingIsBoundedWhateverTheServerSays() {
    std::printf("Pacing, against readings it cannot trust:\n");

    NetPacing pacing;
    check("nothing reported is exactly the wall clock", pacing.scale() == 1.0f);

    for (int i = 0; i < 1000; ++i) pacing.report(0, 0.1f, 128.0f);
    check(
        "a queue always empty hurries the client",
        pacing.scale() > 1.0f && pacing.scale() <= 1.0f + NetPacing::MAX_DILATION
    );

    pacing.clear();
    for (int i = 0; i < 1000; ++i) pacing.report(std::numeric_limits<uint32_t>::max(), 0.1f, 128.0f);
    check(
        "a queue always too deep holds it back",
        pacing.scale() < 1.0f && pacing.scale() >= 1.0f - NetPacing::MAX_DILATION
    );
    check(
        "  and a reading past what the wire can say counts as the most it can",
        pacing.depth() <= static_cast<float>(NET_QUEUE_DEPTH_MAX)
    );

    pacing.clear();
    check("cleared, it is the wall clock again", pacing.scale() == 1.0f);
}

/// What a minute of two clocks did to the server's queue of one client's commands.
struct QueueMinute {
    uint32_t repeated = 0;     ///< Ticks the server ran on a repeat.
    uint32_t skipped  = 0;     ///< Commands it never ran.
    size_t   deepest  = 0;     ///< Most commands waiting at any take.
    float    lowWater = 0.0f;  ///< The mean of what the snapshots carried.
    float    slowest  = std::numeric_limits<float>::max();  ///< The pacing's range over the minute.
    float    fastest  = 0.0f;
    float    wobble   = 0.0f;  ///< How far the pacing strays from its own mean, as a deviation.
};

/// A link and the rate both ends tick at.
struct TwoClockLink {
    double latency  = 0.030;  ///< One way, in seconds, before jitter.
    double tickRate = 128.0;
};

/**
 * @brief A client and a server on clocks @p drift apart, across a jittering link.
 *
 * Every datagram takes the latency plus up to 15 ms, so packets bunch and reorder. The
 * client draws at 144 Hz and sends the newest NET_COMMAND_REDUNDANCY commands 64 times
 * a second; the server sends the low water as often. Ten seconds settle, then a minute
 * is counted.
 *
 * @param drift  How much faster the client's clock runs, as a fraction; 0.03 is 3% fast.
 * @param paced  Whether the client ticks by its pacing.
 * @param link   The one-way latency and the rate both ends tick at.
 * @return What the counted minute did to the server's queue and the client's pacing.
 */
QueueMinute runTwoClocks(float drift, bool paced, const TwoClockLink& link) {
    const double     TICK    = 1.0 / link.tickRate;
    const double     LATENCY = link.latency;
    constexpr double SEND    = 1.0 / 64.0;
    constexpr double FRAME   = 1.0 / 144.0;
    constexpr double JITTER  = 0.015;
    constexpr double STEP    = 0.0005;
    constexpr double SETTLE  = 10.0;
    constexpr double COUNTED = 60.0;

    struct Datagram {
        double                    due = 0.0;
        uint32_t                  sequence = 0;
        uint32_t                  lowWater = 0;
        std::vector<InputCommand> commands;
    };

    Math::Rng rng(7);
    NetCommandBuffer server;
    NetPacing pacing;
    std::vector<Datagram> up;
    std::vector<Datagram> down;

    std::vector<InputCommand> newest;
    double   clientFrame = 0.0, clientOwed = 0.0, clientSend = 0.0;
    uint32_t clientTick  = 0, heard = 0;
    double   serverOwed  = 0.0, serverSend = 0.0;
    uint32_t serverTick  = 0, snapshot = 0;

    QueueMinute minute;
    uint32_t repeatedBefore = 0, skippedBefore = 0, readings = 0;
    double   lowWaterSum = 0.0;
    double   scaleSum = 0.0, scaleSquares = 0.0, scaleFrames = 0.0;
    bool     counting = false;

    for (double now = 0.0; now < SETTLE + COUNTED; now += STEP) {
        if (!counting && now >= SETTLE) {
            counting       = true;
            repeatedBefore = server.repeatedTicks();
            skippedBefore  = server.skippedCommands();
        }

        // The server: what has arrived, the ticks it owes, and a snapshot when due.
        for (auto it = up.begin(); it != up.end();) {
            if (it->due > now) {
                ++it;
                continue;
            }
            for (const InputCommand& command : it->commands) server.accept(command);
            it = up.erase(it);
        }
        serverOwed += STEP;
        while (serverOwed >= TICK) {
            serverOwed -= TICK;
            if (counting) minute.deepest = std::max(minute.deepest, server.pending());
            server.take(++serverTick);
        }
        serverSend += STEP;
        if (serverSend >= SEND) {
            serverSend -= SEND;
            const uint32_t low = static_cast<uint32_t>(
                std::min<size_t>(server.lowWater(), NET_QUEUE_DEPTH_MAX)
            );
            server.restartLowWater();
            if (counting) {
                lowWaterSum += low;
                ++readings;
            }
            down.push_back({now + LATENCY + JITTER * rng.nextFloat(), ++snapshot, low, {}});
        }

        // The client, a frame at a time on its drifting clock: ticks by pacing, a packet
        // when due, then what came down. Snapshots older than one read are refused.
        clientFrame += STEP * (1.0 + drift);
        if (clientFrame < FRAME) continue;
        const double frame = clientFrame;
        clientFrame = 0.0;

        clientOwed += frame * (paced ? pacing.scale() : 1.0f);
        while (clientOwed >= TICK) {
            clientOwed -= TICK;
            ++clientTick;
            newest.push_back(walkCommand(clientTick, clientTick, 1.0f, false));
            if (newest.size() > NET_COMMAND_REDUNDANCY) newest.erase(newest.begin());
        }
        clientSend += frame;
        if (clientSend >= SEND) {
            clientSend = std::min(clientSend - SEND, SEND);
            up.push_back({now + LATENCY + JITTER * rng.nextFloat(), 0, 0, newest});
        }
        for (auto it = down.begin(); it != down.end();) {
            if (it->due > now) {
                ++it;
                continue;
            }
            if (it->sequence > heard) {
                heard = it->sequence;
                // The round trip a connection measures: both ways, plus twice the jitter's mean.
                pacing.report(
                    it->lowWater,
                    static_cast<float>(2.0 * LATENCY + JITTER),
                    static_cast<float>(link.tickRate)
                );
            }
            it = down.erase(it);
        }
        if (counting) {
            minute.slowest = std::min(minute.slowest, pacing.scale());
            minute.fastest = std::max(minute.fastest, pacing.scale());
            scaleSum       += pacing.scale();
            scaleSquares   += static_cast<double>(pacing.scale()) * pacing.scale();
            ++scaleFrames;
        }
    }

    const double meanScale = scaleSum / std::max(scaleFrames, 1.0);
    const double variance  = scaleSquares / std::max(scaleFrames, 1.0) - meanScale * meanScale;
    minute.wobble   = static_cast<float>(std::sqrt(std::max(0.0, variance)));
    minute.repeated = server.repeatedTicks() - repeatedBefore;
    minute.skipped  = server.skippedCommands() - skippedBefore;
    minute.lowWater = readings > 0 ? static_cast<float>(lowWaterSum / readings) : 0.0f;
    return minute;
}

// Two clocks at one nominal rate drift. Unsteered, a client a few per cent fast fills
// the server's queue until trimmed, dropping input; a few per cent slow drains it until
// ticks run on a repeat - a misprediction replayed every few ticks. Paced, it sits a
// command or two above empty on long and short links and at every rate: the loop's
// gain is set by its own delay, or a long link answers each reading a round trip late
// and swings between bounds.
void testAClientPacesItsTicksToTheServersQueue() {
    std::printf("A client clock 3%% off the server's, across a link that jitters:\n");

    const TwoClockLink links[] = {
        {0.030, static_cast<double>(NET_MAX_TICK_RATE)},          // a short link, at the fastest rate
        {0.200, static_cast<double>(NET_MAX_TICK_RATE)},          // a long one
        {0.030, static_cast<double>(Config::DEFAULT_TICK_RATE)},  // the engine's default rate
    };
    for (const TwoClockLink& link : links) {
        std::printf("    %.0f ms each way, %.0f Hz:\n", link.latency * 1000.0, link.tickRate);
        for (const float drift : {0.03f, -0.03f}) {
            const QueueMinute free  = runTwoClocks(drift, false, link);
            const QueueMinute paced = runTwoClocks(drift, true, link);
            const uint32_t freeLost  = free.repeated + free.skipped;
            const uint32_t pacedLost = paced.repeated + paced.skipped;

            char label[96];
            std::snprintf(
                label,
                sizeof(label),
                "  %s, unpaced, the queue %s every few ticks",
                drift > 0.0f ? "fast" : "slow",
                drift > 0.0f ? "is trimmed" : "runs dry"
            );
            check(label, freeLost > 100);
            std::snprintf(
                label,
                sizeof(label),
                "  %s, paced, it neither runs dry nor is trimmed",
                drift > 0.0f ? "fast" : "slow"
            );
            check(label, pacedLost * 20 <= freeLost && pacedLost <= 5);
            check(
                "    holding its low water a command or two above empty",
                paced.lowWater >= 1.0f && paced.lowWater <= 4.0f
            );
            check(
                "    and its top below where a burst is trimmed",
                paced.deepest <= NetCommandBuffer::MAX_DEPTH
            );
            check(
                "    by bending its pacing no further than the bound",
                paced.slowest >= 1.0f - NetPacing::MAX_DILATION
                    && paced.fastest <= 1.0f + NetPacing::MAX_DILATION
            );
            check(
                "    and settling rather than swinging between the bounds",
                paced.wobble < NetPacing::MAX_DILATION * 0.3f
            );
            std::printf(
                "      drift %+.0f%%: unpaced %u repeated and %u skipped a minute, "
                "paced %u and %u, low water %.1f, deepest %zu, pacing %.3f to %.3f "
                "(deviation %.4f)\n",
                static_cast<double>(drift * 100.0f),
                free.repeated,
                free.skipped,
                paced.repeated,
                paced.skipped,
                static_cast<double>(paced.lowWater),
                paced.deepest,
                static_cast<double>(paced.slowest),
                static_cast<double>(paced.fastest),
                static_cast<double>(paced.wobble)
            );
        }
    }
}

// Per NET_MAX_TICK_RATE, the queue's bounds count commands, sized for two a packet. A
// faster session is refused at once, with a reason, rather than trimming and repeating
// input.
void testASessionRefusesARateItsQueueCannotHold() {
    std::printf("A project ticking faster than a networked game runs:\n");

    NetSession server;
    check("a server at the fastest rate a session runs binds", server.host(0, 2, NET_MAX_TICK_RATE));
    server.close();
    check("one any faster is refused", !server.host(0, 2, NET_MAX_TICK_RATE + 1));
    check("  saying why", !server.lastError().empty());
    check("  and stays offline", server.isOffline());

    NetSession client;
    check(
        "a client any faster is refused",
        !client.connect(NetAddress{0x7F000001u, 9}, NET_MAX_TICK_RATE + 1)
    );
    check("  and stays offline", client.isOffline());
}

} // namespace

void runNetPredictionTests() {
    testAJumpSurvivesTheNetworkLosingIt();
    testATickWithNoCommandStillRuns();
    testALinkThatHasGoneStopsTheBodyRatherThanWalkingIt();
    testCommandsRunInTheOrderTheyWereMade();
    testACommandIsNeverNumberedAcrossAGap();
    testAPacketCarriesWhatTheServerHasNotHeard();
    testWhatAFrameOfInputCostsOnTheWire();
    testAPacketCannotTalkTheDecoderPastTheEndOfACommand();
    testTheWorldIsDrawnSmoothlyBetweenWhatArrives();
    testAServerUpForDaysIsDrawnAsSmoothlyAsANewOne();
    testTheDelayRidesOutTheJitterItMeasures();
    testSmoothingNeverFeedsItselfItsOwnGuess();
    testAPredictionThatWasWrongIsRunAgainRatherThanArguedWith();
    testAShotIsJudgedAgainstWhatTheShooterCouldSee();
    testAPlayerIsNotDraggedBackwardsByTheirOwnConnection();
    testPacingIsBoundedWhateverTheServerSays();
    testAClientPacesItsTicksToTheServersQueue();
    testASessionRefusesARateItsQueueCannotHold();
}
