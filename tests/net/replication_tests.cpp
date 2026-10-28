#include "physics/physics_support.h"
#include "net/net_support.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "ecs/component/core/net_local.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "net/net_session.h"
#include "net/replication/silence.h"
#include "net/wire/codecs.h"
#include "system/physics/authoring/ragdoll_build.h"

namespace {

void testTheFirstSnapshotIsTheWholeWorld() {
    std::printf("Joining and playing are the same code path:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 40);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    const NetSnapshotStats first = writeSnapshotTo(server, schema, 1, budget, baseline, body);
    check("a connection that has confirmed nothing is sent everything", first.entitiesWritten == 40);
    check("with no separate mode for it", first.entitiesDeferred == 0);

    Scene client;
    NetNames clientNames;
    check("and the client takes it", readSnapshotFrom(client, schema, body, clientNames));
    check("building every entity it had never heard of", client.entityCount() == 40);

    const EntityId last = client.entityAt(crates.back().slot());
    check(
        "at the position the server had it",
        std::abs(client.get<Transform>(last).position.x - 39 * 1.5f) < 0.002f
    );

    // Nothing moved and the client confirmed, so the second snapshot is silent - what
    // makes a settled world free, or a still tower of crates costs a packet a tick.
    baseline.confirm(1);
    const NetSnapshotStats second = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("a world that has not changed sends nothing", second.entitiesWritten == 0);
    std::printf(
        "      whole world %u B, then %u B once it settled\n",
        first.bytesWritten,
        second.bytesWritten
    );
}

void testALostSnapshotIsSaidAgain() {
    std::printf("The bug that freezes a body on one client for the rest of a match:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 4).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);

    Scene client;
    NetNames clientNames;
    readSnapshotFrom(client, schema, body, clientNames);

    // The crate moves, that snapshot is lost, then it stops. A server that folded the
    // snapshot in when writing it would leave the crate at the old position for good.
    server.get<Transform>(crate).position = {99.0f, 1.0f, 2.0f};
    const NetSnapshotStats moved = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("the move is written", moved.entitiesWritten == 1);

    // Snapshot 2 is never confirmed - it did not arrive.
    const NetSnapshotStats again = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("and while it is unconfirmed the move is written again", again.entitiesWritten == 1);

    readSnapshotFrom(client, schema, body, clientNames);
    baseline.confirm(3);
    check(
        "so the client ends up where the server put it",
        std::abs(client.get<Transform>(client.entityAt(crate.slot())).position.x - 99.0f) < 0.002f
    );

    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 4, budget, baseline, body);
    check("and only then does the server stop saying it", quiet.entitiesWritten == 0);
}

void testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne() {
    std::printf("Acknowledgements arrive in whatever order the network chose:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    server.get<Transform>(crate).position = {1.0f, 0.0f, 0.0f};
    writeSnapshotTo(server, schema, 10, budget, baseline, body);
    server.get<Transform>(crate).position = {2.0f, 0.0f, 0.0f};
    writeSnapshotTo(server, schema, 11, budget, baseline, body);

    // Newer arrives first, then older. Folding the older in after would have the server
    // believe the client holds x=1 while it holds x=2: x=2 resent for nothing, and a
    // crate moving back to x=1 never sent.
    baseline.confirm(11);
    baseline.confirm(10);

    const NetSnapshotStats after = writeSnapshotTo(server, schema, 12, budget, baseline, body);
    check("the older acknowledgement does not overwrite the newer", after.entitiesWritten == 0);
}

// A late acknowledgement of a slot's old occupant, after the new one is confirmed.
void testALateWordOfAnOldOccupantIsMoot() {
    std::printf("A late acknowledgement of what a slot held before:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 2);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    Scene client;
    NetNames clientNames;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);

    server.destroyEntity(crates[1]);
    const EntityId marker = server.createEntity();
    server.add(marker, Transform{});
    writeSnapshotTo(server, schema, 2, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);

    // Newest first: the old crate's word comes after the marker's.
    baseline.confirm(2);
    baseline.confirm(1);

    const NetSnapshotStats after = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);
    check("the old occupant is not reported gone again", after.entitiesLost == 0);
    check("  nor the new one said again", after.entitiesWritten == 0);
    check("  and the client keeps the new one", client.isAlive(client.entityAt(marker.slot())));
}

// A late acknowledgement of an occupant whose leaving was already confirmed.
void testALateWordOfAnEmptiedSlotIsMoot() {
    std::printf("A late acknowledgement of what has since been confirmed gone:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 2);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);
    server.get<Transform>(crates[1]).position = {3.0f, 0.0f, 0.0f};
    writeSnapshotTo(server, schema, 2, budget, baseline, body);

    server.destroyEntity(crates[1]);
    const NetSnapshotStats told = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("the crate is reported gone", told.entitiesLost == 1);

    baseline.confirm(3);
    baseline.confirm(2);

    // Reported again by slot, it would take whatever the slot holds next on the client.
    const NetSnapshotStats after = writeSnapshotTo(server, schema, 4, budget, baseline, body);
    check("the late word does not bring the crate back to be reported", after.entitiesLost == 0);
}

// A component confirmed long ago, then changed and changed back: the sequence wraps every 65536
// snapshots, and a confirmation must still be taken as newer than one a whole turn older, or the
// server would believe the client holds the first value and never say the return to it.
void testAConfirmationAWholeTurnLaterIsStillNewer() {
    std::printf("A value confirmed a whole turn of the sequence ago:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshotTo(server, schema, 100, budget, baseline, body);
    baseline.confirm(100);

    // Idle until the sequence has come round to just short of where the value was confirmed.
    uint16_t sequence = 89;
    server.get<Transform>(crate).position.x += 2.0f;
    writeSnapshotTo(server, schema, ++sequence, budget, baseline, body);
    baseline.confirm(sequence);

    server.get<Transform>(crate).position.x -= 2.0f;
    const NetSnapshotStats back = writeSnapshotTo(server, schema, ++sequence, budget, baseline, body);
    check("the move back is said", back.entitiesWritten == 1);
}

// The receiver's own entity goes in every snapshot whatever else is left out; one too large for a
// snapshot even alone would leave every snapshot unsent, and the client timed out. It is left out
// instead, and the rest of the world still goes.
void testAnOwnerTooLargeForASnapshotLeavesTheRest() {
    std::printf("A player's own entity too large for a snapshot:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 2);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates[0];
    std::vector<uint8_t> body;

    NetBudget probe;
    const NetSnapshotStats whole = writeSnapshotTo(server, schema, 1, probe, baseline, body);
    check("both crates fit an ordinary snapshot", whole.entitiesWritten == 2);

    // The owner's spawn is described in full, which costs more than a whole crate.
    NetSpawn spawn;
    spawn.prefab = "prefabs/crate.json";
    server.add(crates[0], std::move(spawn));

    // Room for the lost list, the end of the entries and two plain crates: the other crate, not the owner.
    budget.bytes = whole.bytesWritten;
    NetBaseline fresh;
    NetSilenceMap silence;
    silence.build(server);
    // Into a datagram exactly the budget's size, as a server writes one.
    BitWriter writer(body, budget.bytes);
    const NetSnapshotStats tight = writeSnapshot(server, schema, silence, 1, budget, fresh, writer);
    writer.finish();
    check("the owner too large is left out, and says so", tight.ownerTooLarge);
    check("  while the other crate is written", tight.entitiesWritten == 1);
    check("  and the snapshot fits, to be sent", !writer.overflowed() && fresh.pendingSnapshots() == 1);
}

void testABurstIsDeferredRatherThanDropped() {
    std::printf("What a collapsing tower does to a packet:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 117);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();
    std::vector<uint8_t> body;

    // Every body awake and moving, far past a datagram's budget: an encoder that refuses
    // rather than defers sends nothing at the moment there is most to see.
    server.forEachEntity([&](EntityId entity) {
        server.get<Rigidbody>(entity).linearVelocity  = {3.0f, -9.0f, 1.5f};
        server.get<Rigidbody>(entity).angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    Scene client;
    NetNames clientNames;
    uint16_t sequence = 1;
    int snapshots = 0;
    uint32_t worst = 0;

    // Until every body has arrived, confirming each snapshot.
    while (snapshots < 20) {
        const NetSnapshotStats stats = writeSnapshotTo(server, schema, sequence, budget, baseline, body);
        worst = std::max(worst, stats.bytesWritten);
        check("no snapshot exceeds what a datagram can carry", stats.bytesWritten <= budget.bytes);
        readSnapshotFrom(client, schema, body, clientNames);
        baseline.confirm(sequence);
        ++sequence;
        ++snapshots;
        // The connection's own entity is written every snapshot, so "nothing to say"
        // is one entry, not none.
        if (stats.entitiesWritten <= 1) break;
    }

    check("every body reached the client", client.entityCount() == 117);
    check("and it took a handful of snapshots, not a hundred", snapshots <= 6);
    std::printf("      117 moving bodies delivered in %d snapshots, worst %u B\n", snapshots, worst);

    // The owner is reconciliation's anchor, so it is never the entity deferred.
    NetBaseline fresh;
    const NetSnapshotStats first = writeSnapshotTo(server, schema, 100, budget, fresh, body);
    check("the burst does not fit one snapshot", first.entitiesDeferred > 0);

    Scene probe;
    NetNames probeNames;
    readSnapshotFrom(probe, schema, body, probeNames);
    check("and the owner is in it anyway", probe.isAliveAtIndex(budget.owner.slot()));
}

void testAnEntityThatLeavesTheWorldLeavesEveryClient() {
    std::printf("What a client is told about an entity that is gone:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 6);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);
    Scene client;
    NetNames clientNames;
    readSnapshotFrom(client, schema, body, clientNames);
    check("six crates to start", client.entityCount() == 6);

    const uint32_t goneSlot = crates[2].slot();
    server.destroyEntity(crates[2]);
    const NetSnapshotStats told = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("the server reports the one that went", told.entitiesLost == 1);

    // Lost before confirmed, so reported again: destruction must be reliable, or the
    // client keeps a body nobody else sees.
    const NetSnapshotStats retold = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("and keeps reporting it until the client confirms", retold.entitiesLost == 1);

    // Named to the caller, so what it keeps per entity (the smoothing's track) can go too.
    const EntityId clientCrate = clientNames.find(client, goneSlot);
    std::vector<EntityId> lost;
    readSnapshotFrom(client, schema, body, clientNames, &lost);
    baseline.confirm(3);
    check("the client destroyed it", client.entityCount() == 5);
    check("and it was the right one", !client.isAliveAtIndex(goneSlot));
    check("  and named it as gone", lost.size() == 1 && lost[0] == clientCrate);

    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 4, budget, baseline, body);
    check("only then does the server stop mentioning it", quiet.entitiesLost == 0);
}

void testASlotReusedBetweenTwoSnapshotsStillReportsWhatLeft() {
    std::printf("An entity destroyed and its slot taken again before the next snapshot:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 4);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    Scene client;
    NetNames clientNames;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);
    baseline.confirm(1);

    // The crate goes and something else takes its slot at once - alive at both sends,
    // so only its occupant says anything changed.
    const EntityId gone = crates[1];
    server.destroyEntity(gone);
    const EntityId marker = server.createEntity();
    server.add(marker, Transform{});
    check("the new entity took the freed slot", marker.slot() == gone.slot());

    const NetSnapshotStats told = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("the old occupant is reported gone", told.entitiesLost == 1);
    readSnapshotFrom(client, schema, body, clientNames);
    baseline.confirm(2);

    // Written onto the crate the client still held, the marker would carry the crate's
    // body - a component the server's entity never had.
    const EntityId mirror = client.entityAt(marker.slot());
    check("the client holds the new entity at that slot", client.isAlive(mirror));
    check("  built fresh, not decoded over the crate that left", !client.has<Rigidbody>(mirror));

    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check(
        "and the report stops once confirmed, without forgetting the new one",
        quiet.entitiesLost == 0 && quiet.entitiesWritten == 0
    );
}

// A snapshot that did not fit its packet is never framed, so its sequence is the next
// packet's. A claim held for it would be confirmed by that packet, and the server
// would stop describing a world the client was never sent.
void testASnapshotThatWasNeverSentClaimsNothing() {
    std::printf("A body its packet had no room for:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 10);

    NetSilenceMap silence;
    silence.build(server);
    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();

    // A packet its header has already filled.
    std::vector<uint8_t> packet;
    BitWriter full(packet, 4);
    full.u32(0);
    writeSnapshot(server, schema, silence, 1, budget, baseline, full);
    check("the packet overflowed", full.overflowed());
    check("  and the snapshot left no claim behind for its sequence", baseline.pendingSnapshots() == 0);

    // The next packet goes under the same sequence; its confirmation folds in only what
    // it carried.
    std::vector<uint8_t> body;
    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);
    check(
        "what is confirmed under that sequence is what was sent",
        baseline.pendingSnapshots() == 0 && baseline.knownEntities() == crates.size()
    );
}

void testASnapshotNeverOutgrowsThePacketItRidesIn() {
    std::printf("The entry that is never deferred, and the budget it can still blow:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);

    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 90);

    // Far up the slot range. An id is a gap from the previous one, and a gap this size
    // costs three times a neighbour's - room is reserved before the gaps are known.
    const EntityId owner = server.createEntityAt(5000);
    Transform at;
    at.position = {40.0f, 4.0f, 0.0f};
    server.add(owner, at);
    server.add(owner, Rigidbody{});

    server.forEach<Rigidbody>([&](EntityId, Rigidbody& body) {
        body.linearVelocity  = {3.0f, -9.0f, 1.5f};
        body.angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    // Every budget where the world does not fit. The owner is never deferred and is last
    // in slot order, so it alone can carry the body past the end of a full packet.
    uint32_t over    = 0;
    uint32_t worst   = 0;
    uint32_t budgets = 0;
    uint32_t ownerless = 0;
    uint32_t tightest  = 0;
    for (uint32_t bytes = 40; bytes <= 1200; ++bytes) {
        NetBaseline baseline;
        NetBudget budget;
        budget.bytes = bytes;
        budget.owner = owner;
        ++budgets;

        std::vector<uint8_t> body;
        const NetSnapshotStats stats = writeSnapshotTo(server, schema, 1, budget, baseline, body);
        if (body.size() > bytes) {
            ++over;
            worst = std::max(worst, static_cast<uint32_t>(body.size()) - bytes);
        }
        // Accumulated, not asserted per budget: a check() in the loop prints a line each.
        if (stats.entitiesWritten < 1) {
            if (ownerless == 0) tightest = bytes;
            ++ownerless;
        }
    }
    check("the owner is in it whatever the budget", ownerless == 0);
    check("and no budget produces a body larger than itself", over == 0);
    std::printf(
        "      %u budgets from 40 to 1200 bytes, %u over by up to %u byte(s), %u without the owner%s\n",
        budgets,
        over,
        worst,
        ownerless,
        ownerless ? (" (first at " + std::to_string(tightest) + ")").c_str() : ""
    );
}

void testABoneTheAnimationPlacesIsNotWorthAPacket() {
    std::printf("What a character costs when the animation already says it:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);

    Scene server;
    addBox(server, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId rig = server.createEntity();
    server.add<Transform>(rig, Transform{});
    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t bones = buildRagdoll(server, rig, skeleton);
    check("the rig has bones to argue about", bones > 0);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    // Inactive: RagdollSystem poses every bone from the animation on both ends, from a
    // clip chosen by a velocity that replicates, so no bone belongs on the wire.
    check("the ragdoll starts inactive", !server.get<Ragdoll>(rig).active);

    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 1, budget, baseline, body);
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the animation places is not described", isPosedByAnimation(server, bone.body));
    }
    const uint32_t withoutBones = quiet.entitiesWritten;
    std::printf("      inactive: %u entities on the wire, %u bones held back\n", withoutBones, bones);

    // A server classifies the world once a round, not per entity per peer; the answer
    // must match the long way.
    NetSilenceMap round;
    round.build(server);
    bool agrees = true;
    server.forEachEntity([&](EntityId entity) {
        agrees = agrees && round.of(entity) == netSilence(server, entity);
    });
    check("  and the round's classification agrees with asking each entity", agrees);

    // Active: the solver drives the bones, so every one travels. A client that missed
    // the switch would pose a walk cycle over a falling body.
    server.get<Ragdoll>(rig).active = true;
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the solver drives is described", !isPosedByAnimation(server, bone.body));
    }

    const NetSnapshotStats loud = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check(
        "switching physics on puts the whole skeleton on the wire",
        loud.entitiesWritten >= withoutBones + bones
    );
    std::printf("      active:   %u entities on the wire\n", loud.entitiesWritten);

    // The switch itself travels, which keeps the two states agreeing.
    baseline.confirm(2);
    Scene client;
    NetNames clientNames;
    check("the client takes it", readSnapshotFrom(client, schema, body, clientNames));
    const EntityId mirror = client.entityAt(rig.slot());
    check(
        "the client learned the ragdoll is active",
        client.isAlive(mirror) && client.has<Ragdoll>(mirror) && client.get<Ragdoll>(mirror).active
    );
}

void testASpawnRefusesWhatItCannotMean() {
    std::printf("A NetSpawn, and the values it must not carry:\n");

    NetSpawn spawn;
    spawn.prefab      = "prefabs/crate.json";
    spawn.at.position = {3.0f, 4.0f, 5.0f};
    spawn.at.rotation = glm::angleAxis(glm::radians(37.3f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
    spawn.at.scale    = {2.0f, 0.5f, 3.25f};

    // A marker follows each case, so a refusal is seen to leave the stream in step.
    constexpr uint32_t MARKER = 0xC0FFEEu;
    const auto roundTrip = [&](const NetSpawn& in, NetSpawn& out) {
        std::vector<uint8_t> bytes;
        BitWriter writer(bytes, 512);
        netEncode(in, writer);
        writer.u32(MARKER);
        writer.finish();
        BitReader reader(bytes.data(), bytes.size());
        out.prefab = "left over";
        netDecode(out, reader);
        return reader.u32() == MARKER && !reader.failed();
    };

    NetSpawn back;
    check("an ordinary spawn is read back in step", roundTrip(spawn, back));
    check("  to the same path", back.prefab == spawn.prefab);
    check(
        "  at full precision, since nothing later corrects it",
        back.at.position.x == 3.0f && back.at.position.z == 5.0f
    );
    // A static body is never described again: a rotation rounded or a scale dropped
    // here stays wrong for good.
    check(
        "  with the rotation exact, not quantised",
        back.at.rotation.w == spawn.at.rotation.w
            && back.at.rotation.x == spawn.at.rotation.x
            && back.at.rotation.y == spawn.at.rotation.y
            && back.at.rotation.z == spawn.at.rotation.z
    );
    check("  and the scale it was built at", back.at.scale == spawn.at.scale);

    // Where each entity below the root was built, so a client builds it there.
    NetSpawn placed = spawn;
    placed.slots = {{7, 40}, {9, 41}, {12, 3}};
    check("the slots it names are read back", roundTrip(placed, back) && back.slots == placed.slots);

    NetSpawn intoNothing = placed;
    intoNothing.slots[13] = 0;
    check("a slot of zero names nothing and is refused", roundTrip(intoNothing, back) && back.prefab.empty());

    NetSpawn crowded = spawn;
    for (uint32_t i = 0; i <= NET_SPAWN_MAX_SLOTS; ++i) crowded.slots[i] = 100 + i;
    check("more slots than a spawn may name cannot be said", !isSayable(crowded));
    check("  and the reader stays in step past it", roundTrip(crowded, back) && back.prefab.empty());

    // A path the length field cannot describe: written whole, the length would wrap
    // and every later byte be read at the wrong offset.
    NetSpawn tooLong = spawn;
    tooLong.prefab.assign(NET_PROJECT_PATH_MAX + 1, 'x');
    check("a path longer than the wire can name cannot be said", !isSayable(tooLong));
    check("  and is written as a refusal the reader stays in step past", roundTrip(tooLong, back));
    check("  which builds nothing", back.prefab.empty());

    NetSpawn nowhere = spawn;
    nowhere.prefab.clear();
    check("an empty path is refused", roundTrip(nowhere, back) && back.prefab.empty());

    NetSpawn unturned = spawn;
    unturned.at.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    check("a rotation of no length is refused", roundTrip(unturned, back) && back.prefab.empty());

    // Clamped at the reader, the server's body and every client's would differ in
    // size for good.
    NetSpawn huge = spawn;
    huge.at.scale = {2.0f * NET_MAX_SCALE, 1.0f, 1.0f};
    check("a scale past what the wire carries cannot be said", !isSayable(huge));
    check("  and is refused rather than clamped", roundTrip(huge, back) && back.prefab.empty());

    // The pose travels as raw floats, where a sender can put anything; a NaN in a
    // Transform spreads. Forged by hand, since the encoder will not write one.
    std::vector<uint8_t> poisoned;
    BitWriter forged(poisoned, 512);
    forged.bits(static_cast<uint32_t>(spawn.prefab.size()), 8);
    for (const char c : spawn.prefab) forged.bits(static_cast<uint8_t>(c), 8);
    forged.f32(std::numeric_limits<float>::quiet_NaN());
    forged.f32(4.0f);
    forged.f32(5.0f);
    forged.f32(spawn.at.rotation.w);
    forged.f32(spawn.at.rotation.x);
    forged.f32(spawn.at.rotation.y);
    forged.f32(spawn.at.rotation.z);
    forged.f32(spawn.at.scale.x);
    forged.f32(spawn.at.scale.y);
    forged.f32(spawn.at.scale.z);
    forged.bits(0, 6);   // no slots
    forged.u32(MARKER);
    forged.finish();

    BitReader poison(poisoned.data(), poisoned.size());
    NetSpawn read;
    netDecode(read, poison);
    check("a spawn at a position that is not a number is refused", read.prefab.empty());
    check("  and the stream is still in step after it", poison.u32() == MARKER);
}

// Scenery every end moves for itself stays off the wire, and so does all below it: a joiner is
// not told where drifting scenery is, nor held to it.
void testWhatEachEndRunsItselfIsNeverSent() {
    std::printf("What each end runs for itself:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;

    const EntityId world = server.createEntity();
    server.add(world, Transform{});
    server.add(world, NetLocal{});
    const EntityId drifter = server.createEntity();
    Transform distant;
    distant.scale = glm::vec3(5000.0f);  // past what the wire carries
    server.add(drifter, distant);
    HierarchyOperations::setParent(server, drifter, world);
    const EntityId player = server.createEntity();
    server.add(player, Transform{});

    check("a NetLocal entity is off the wire", netSilence(server, world) == NetSilence::Local);
    check("  and so is one below it", netSilence(server, drifter) == NetSilence::Local);
    check("  but not an entity beside them", netSilence(server, player) == NetSilence::None);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    const NetSnapshotStats told = writeSnapshotTo(server, schema, 1, budget, baseline, body);
    check("a joiner's first snapshot describes only the entity beside them", told.entitiesWritten == 1);

    Scene client;
    NetNames clientNames;
    readSnapshotFrom(client, schema, body, clientNames);
    check("  and the client holds nothing of the scenery", !client.isAliveAtIndex(drifter.slot()));
}

void testWhatASpawnedRootIsGoesOutWhenItsStateDoesNot() {
    std::printf("A spawned static body, and the entity a peer drives:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;

    // A static body: its pose is the scene's, exactly, and the wire would round it.
    // What it is must still reach everyone.
    const EntityId wall = server.createEntity();
    server.add(wall, Transform{});
    Rigidbody stone;
    stone.motion = RigidbodyMotion::Static;
    server.add(wall, stone);
    server.add(wall, NetSpawn{"prefabs/wall.json", Transform{}, {}});

    // The same body with nothing spawned about it stays silent.
    const EntityId authored = server.createEntity();
    server.add(authored, Transform{});
    server.add(authored, stone);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    const NetSnapshotStats told = writeSnapshotTo(server, schema, 1, budget, baseline, body);
    check("the spawned static body is described", told.entitiesWritten == 1);
    check("  by what it is and nothing else", told.componentsWritten == 1);

    Scene client;
    NetNames clientNames;
    readSnapshotFrom(client, schema, body, clientNames);
    const EntityId mirror = client.entityAt(wall.slot());
    check("the client holds what it is", client.isAlive(mirror) && client.has<NetSpawn>(mirror));
    check("  and not a quantised copy of where it stands", !client.has<Transform>(mirror));
    check("the authored static body is not described at all", !client.isAliveAtIndex(authored.slot()));

    baseline.confirm(1);
    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("once confirmed it is not said again", quiet.entitiesWritten == 0);

    // The receiver's own entity is written every snapshot, as it holds its prediction;
    // what it is was never predicted.
    Scene world;
    const EntityId player = world.createEntity();
    world.add(player, Transform{});
    world.add(player, Rigidbody{});
    world.add(player, NetSpawn{"prefabs/player.json", Transform{}, {}});

    NetBaseline peer;
    NetBudget forPlayer;
    forPlayer.owner = player;
    const NetSnapshotStats first = writeSnapshotTo(world, schema, 1, forPlayer, peer, body);
    check("a peer's own spawned entity arrives whole", first.componentsWritten == 3);
    peer.confirm(1);
    const NetSnapshotStats again = writeSnapshotTo(world, schema, 2, forPlayer, peer, body);
    check(
        "  and is then resent without what it is",
        again.entitiesWritten == 1 && again.componentsWritten == 2
    );
}

/**
 * @brief Write a prefab of a root with a body and @p parts children, as the editor
 *        writes one from a live subtree.
 *
 * @param path      Destination file, project-relative or absolute.
 * @param resources Resolves asset handles to names.
 * @param parts     How many children the root gets.
 * @param motion    The root body's motion; Static keeps its state off the wire.
 * @return Whether Prefab::save wrote it.
 */
bool writeSpawnablePrefab(
    const std::string& path,
    ResourceManager& resources,
    int parts,
    RigidbodyMotion motion = RigidbodyMotion::Dynamic
) {
    Scene authoring;
    const EntityId root = authoring.createEntity();
    Transform at;
    at.position = {1.0f, 2.0f, 3.0f};
    authoring.add(root, at);
    Rigidbody body;
    body.motion = motion;
    authoring.add(root, body);
    authoring.add(root, makeName("SpawnedThing"));
    for (int i = 0; i < parts; ++i) {
        const EntityId child = authoring.createEntity();
        Transform local;
        local.position = {static_cast<float>(i), 0.5f, 0.0f};
        authoring.add(child, local);
        authoring.add(child, makeName("Part"));
        HierarchyOperations::setParent(authoring, child, root);
    }
    return Prefab::save(authoring, root, path, resources);
}

/// A spawn test's prefab path in the match's scratch project, as a server names it.
std::string scratchPrefab(const char* name) {
    return std::string("prefabs/") + name;
}

/**
 * @brief A server holding a few crates and one client joined on loopback, in a
 *        scratch project of their own.
 *
 * Both ends resolve spawned paths against the project, which goes with the match.
 */
class SpawnMatch {
    public:
        explicit SpawnMatch(int crates) {
            buildCrates(m_serverWorld, crates);
            m_server.onSpawn(
                [](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
                [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
            );
            m_server.host(0, 4, TEST_TICK_RATE);
        }

        ~SpawnMatch() {
            m_client.close();
            m_server.close();
        }

        SpawnMatch(const SpawnMatch& other) = delete;
        SpawnMatch& operator=(const SpawnMatch& other) = delete;

        SpawnMatch(SpawnMatch && other) = delete;
        SpawnMatch& operator=(SpawnMatch && other) = delete;

    public:
        /// Join, and wait for everything the server holds now to arrive.
        bool join() {
            m_client.setWorld(0, m_clientWorld);
            return joinTheWorldItWasTold();
        }

        /// Join without telling the client its world first, as a host joining from its menu.
        bool joinTheWorldItWasTold() {
            m_client.connect(NetAddress{0x7F000001u, m_server.localAddress().port}, TEST_TICK_RATE);
            const size_t world = m_serverWorld.entityCount();
            return pumpUntil([&]() { frame(); }, [&]() { return m_client.isPlaying(); })
                && pumpUntil([&]() { frame(); }, [&]() { return m_clientWorld.entityCount() >= world; }, 80);
        }

        /// The client leaves, and a frame passes, as a host's would.
        void leave() {
            m_client.close();
            frame();
        }

        /// One frame of both ends, in a host's order.
        void frame() {
            m_server.receive(m_serverWorld, m_resources);
            m_client.receive(m_clientWorld, m_resources);
            ++m_tick;
            m_server.beginTick(m_tick, InputCommand{}, 3, TEST_ACTION_NAMES);
            m_client.beginTick(m_tick, InputCommand{}, 3, TEST_ACTION_NAMES);
            m_server.send(m_serverWorld, m_tick);
            m_client.send(m_clientWorld, m_tick);
            m_server.advance(1.0f / 60.0f);
            m_client.advance(1.0f / 60.0f);
        }

        /// What the server's @p root is on the client, if it has arrived.
        EntityId mirrorOf(EntityId root) const { return m_client.entityFor(m_clientWorld, root.slot()); }

        /// Whether @p root has arrived at the client built from its prefab.
        bool isBuilt(EntityId root, int parts) const {
            const EntityId mirror = mirrorOf(root);
            return m_clientWorld.isAlive(mirror) && m_clientWorld.has<PrefabInstance>(mirror)
                && childrenOf(m_clientWorld, mirror) == parts;
        }

    public:
        ResourceManager& resources()   { return m_resources; }
        Scene&           serverWorld() { return m_serverWorld; }
        Scene&           clientWorld() { return m_clientWorld; }
        NetSession&      server()      { return m_server; }
        NetSession&      client()      { return m_client; }

    private:
        ScratchProject  m_project{"vkm_net_spawn"};
        ResourceManager m_resources;
        Scene           m_serverWorld;
        Scene           m_clientWorld;
        NetSession      m_server;
        NetSession      m_client;
        uint32_t        m_tick = 0;
};

void testSomethingBuiltAfterTheSceneLoadedReachesEveryone() {
    std::printf("A thing that did not exist when either end opened the world:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(5);

    const std::string prefabPath = scratchPrefab("net_spawn_test.json");
    check("a prefab is written from a live subtree", writeSpawnablePrefab(prefabPath, match.resources(), 2));
    check("the client joins and the authored world arrives", match.join());

    // Build something neither end had - the feature's reason: a client told only where
    // an entity is, never what, would draw nothing.
    Scene& serverWorld = match.serverWorld();
    Scene& clientWorld = match.clientWorld();
    Transform where;
    where.position = {-12.5f, 4.25f, 7.0f};
    // What a client would refuse, the server does not build: otherwise the two ends
    // disagree on the world for the match.
    const size_t held = serverWorld.entityCount();
    // The same file, reached by leaving the project and coming back: it opens, so only
    // the rule can refuse it.
    const std::string outside =
        "../" + ProjectPaths::projectRoot().filename().string() + "/" + prefabPath;
    Transform enormous = where;
    enormous.scale = glm::vec3(2.0f * NET_MAX_SCALE);
    check(
        "the server will not build a prefab named from outside the project",
        !match.server().spawn(serverWorld, match.resources(), outside, where)
    );
    check(
        "  nor one at a scale the wire cannot carry",
        !match.server().spawn(serverWorld, match.resources(), prefabPath, enormous)
    );
    check("  and built nothing trying", serverWorld.entityCount() == held);

    // The client's slots have their own history, so the slots it would pick for the
    // children are not the server's.
    std::vector<EntityId> scratch;
    for (int i = 0; i < 4; ++i) scratch.push_back(clientWorld.createEntity());
    for (const size_t i : {1u, 3u, 0u, 2u}) clientWorld.destroyEntity(scratch[i]);

    const EntityId spawned = match.server().spawn(serverWorld, match.resources(), prefabPath, where);
    check("the server builds it", serverWorld.isAlive(spawned));
    check("as a subtree, not one entity", childrenOf(serverWorld, spawned) == 2);

    // The prefab authored it at rest; the build must not put that back over the
    // server's word.
    serverWorld.get<Rigidbody>(spawned).linearVelocity = {0.0f, 0.0f, 3.0f};

    const auto frame = [&]() { match.frame(); };
    check(
        "and it reaches the client built",
        pumpUntil(frame, [&]() { return match.isBuilt(spawned, 2); }, 80)
    );

    const EntityId mirror = clientWorld.entityAt(spawned.slot());
    check("at the same slot, which is its name on the wire", clientWorld.isAlive(mirror));
    check(
        "the subtree built from the same file, not sent over the wire",
        childrenOf(clientWorld, mirror) == 2
    );
    check(
        "marked as an instance, so a later save writes the reference",
        clientWorld.has<PrefabInstance>(mirror)
    );
    check(
        "its children in the slots the server built them in, whatever the client's history",
        Prefab::builtSlotsOf(clientWorld, mirror) == Prefab::builtSlotsOf(serverWorld, spawned)
    );
    check(
        "placed where the server placed it",
        glm::length(clientWorld.get<Transform>(mirror).position - where.position) < 0.002f
    );
    check(
        "with what the server said of its body kept over what the file authored",
        clientWorld.has<Rigidbody>(mirror)
            && std::abs(clientWorld.get<Rigidbody>(mirror).linearVelocity.z - 3.0f) < 0.01f
    );

    // Only the root replicates; both ends built the children from the same file. Moving
    // the root must still move everything.
    serverWorld.get<Transform>(spawned).position = {40.0f, 1.0f, -2.0f};
    const auto mirrored = [&]() {
        return std::abs(clientWorld.get<Transform>(mirror).position.x - 40.0f) < 0.002f;
    };
    check("and moving it afterwards reaches the client too", pumpUntil(frame, mirrored, 80));

    // Destroying the root's subtree on the server is the whole despawn: the snapshot
    // reports the root gone, and the client takes the subtree it built with it.
    const size_t before = clientWorld.entityCount();
    HierarchyOperations::destroyHierarchy(serverWorld, spawned);
    check("the server drops it", !serverWorld.isAliveAtIndex(spawned.slot()));
    check(
        "and so does the client",
        pumpUntil(frame, [&]() { return !clientWorld.isAliveAtIndex(spawned.slot()); }, 80)
    );
    check("taking its children with it, not leaking them", clientWorld.entityCount() == before - 3);
}

// Presence-delta describes every new root at once, but a packet holds only so many,
// and a client builds NET_SPAWN_BUILDS_PER_FRAME a frame. None may end a bare body.
void testABurstOfSpawnsArrivesBuiltRatherThanBare() {
    std::printf("More things built in one tick than a packet or a frame can take:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(3);

    const std::string prefabPath = scratchPrefab("net_spawn_burst_test.json");
    check("a prefab is written", writeSpawnablePrefab(prefabPath, match.resources(), 2));
    check("the client joins and the authored world arrives", match.join());

    const int burst = static_cast<int>(NET_SPAWN_BUILDS_PER_FRAME) * 3 + 1;
    std::vector<EntityId> spawned;
    for (int i = 0; i < burst; ++i) {
        Transform where;
        where.position = {static_cast<float>(i) * 2.0f, 1.0f, -4.0f};
        spawned.push_back(match.server().spawn(match.serverWorld(), match.resources(), prefabPath, where));
    }

    // Counted per frame, to see the cap hold and the rest wait.
    int builtBefore   = 0;
    int mostInAFrame  = 0;
    int mostWaiting   = 0;
    int frames        = 0;
    for (; frames < 120; ++frames) {
        match.frame();
        int built = 0;
        int waiting = 0;
        for (EntityId root : spawned) {
            if (match.isBuilt(root, 2)) ++built;
            else if (match.clientWorld().isAliveAtIndex(root.slot())) ++waiting;
        }
        mostInAFrame = std::max(mostInAFrame, built - builtBefore);
        mostWaiting  = std::max(mostWaiting, waiting);
        builtBefore  = built;
        if (built == burst) break;
    }

    check("every one of them reaches the client as the prefab, not a bare body", builtBefore == burst);
    check(
        "  no frame building more than its share",
        mostInAFrame <= static_cast<int>(NET_SPAWN_BUILDS_PER_FRAME)
    );
    check("  the rest having waited, still holding what they were told", mostWaiting > 0);
    check("  and all of it within a few frames", frames < 20);
    std::printf(
        "      %d of %d spawned roots built in %d frames, at most %d in one, %d waiting at most\n",
        builtBefore,
        burst,
        frames + 1,
        mostInAFrame,
        mostWaiting
    );
}

// A spawn travels as state, so a late joiner is told exactly as one who was there.
void testAPlayerWhoJoinsLateSeesWhatWasSpawned() {
    std::printf("Three things spawned before anyone joined:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);

    const std::string prefabPath = scratchPrefab("net_spawn_late_test.json");
    check("a prefab is written", writeSpawnablePrefab(prefabPath, match.resources(), 2));

    std::vector<EntityId> spawned;
    for (int i = 0; i < 3; ++i) {
        Transform where;
        where.position = {static_cast<float>(i) * 3.0f, 0.5f, 6.0f};
        spawned.push_back(match.server().spawn(match.serverWorld(), match.resources(), prefabPath, where));
    }
    check(
        "the server built all three",
        std::all_of(
            spawned.begin(),
            spawned.end(),
            [&](EntityId e) { return match.serverWorld().isAlive(e); }
        )
    );

    check("then a client joins", match.join());
    const auto frame = [&]() { match.frame(); };
    const auto allBuilt = [&]() {
        return std::all_of(spawned.begin(), spawned.end(), [&](EntityId e) { return match.isBuilt(e, 2); });
    };
    check("and sees every one of them built", pumpUntil(frame, allBuilt, 80));
}

// A static body is never described, so its build pose is the one it keeps, and only
// the spawn carries it.
void testASpawnedStaticBodyKeepsItsExactPose() {
    std::printf("A wall spawned at a pose the wire could only round:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);

    const std::string prefabPath = scratchPrefab("net_spawn_static_test.json");
    check(
        "a prefab with a static root is written",
        writeSpawnablePrefab(prefabPath, match.resources(), 1, RigidbodyMotion::Static)
    );
    check("the client joins", match.join());

    Transform where;
    where.position = {3.1234567f, 0.0004321f, -250.98765f};
    where.rotation = glm::angleAxis(glm::radians(37.3f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
    where.scale    = {2.5f, 1.0f, 0.75f};
    const EntityId wall = match.server().spawn(match.serverWorld(), match.resources(), prefabPath, where);
    check("the server builds it", match.serverWorld().isAlive(wall));
    check("  as a body the wire says nothing about", isStaticBody(match.serverWorld(), wall));

    const auto frame = [&]() { match.frame(); };
    check("and the client builds it too", pumpUntil(frame, [&]() { return match.isBuilt(wall, 1); }, 80));

    const Transform& held = match.clientWorld().get<Transform>(match.clientWorld().entityAt(wall.slot()));
    check("  at exactly the position it was spawned at", held.position == where.position);
    check("  the rotation, not a quantised copy of it", held.rotation == where.rotation);
    check("  and the scale", held.scale == where.scale);
}

// Destroyed and respawned into between two snapshots: the client hears the old root
// left before what the new one is, so the new prefab is built, not the old kept.
void testASlotSpawnedIntoAgainBuildsTheNewThing() {
    std::printf("One prefab destroyed and another spawned into its slot at once:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);

    const std::string firstPath  = scratchPrefab("net_spawn_first_test.json");
    const std::string secondPath = scratchPrefab("net_spawn_second_test.json");
    check(
        "two prefabs are written",
        writeSpawnablePrefab(firstPath, match.resources(), 0)
            && writeSpawnablePrefab(secondPath, match.resources(), 0)
    );
    check("the client joins", match.join());

    const EntityId first = match.server().spawn(
        match.serverWorld(),
        match.resources(),
        firstPath,
        Transform{}
    );
    const auto frame = [&]() { match.frame(); };
    check(
        "the first is built on the client",
        pumpUntil(frame, [&]() { return match.isBuilt(first, 0); }, 80)
    );

    HierarchyOperations::destroyHierarchy(match.serverWorld(), first);
    const EntityId second = match.server().spawn(
        match.serverWorld(),
        match.resources(),
        secondPath,
        Transform{}
    );
    check("the second took the first one's slot", second.slot() == first.slot());

    const auto isSecond = [&]() {
        if (!match.isBuilt(second, 0)) return false;
        const EntityId mirror = match.mirrorOf(second);
        return match.clientWorld().get<PrefabInstance>(mirror).source == secondPath;
    };
    check("and the client ends holding the second, not the first", pumpUntil(frame, isSecond, 80));
}

// A client's world replaced mid-session by the same world - a level loaded again - is the world it
// shares with the server, slot for slot: what the server says of a crate moves the new world's
// crate rather than building a second.
void testAWorldReplacedMidSessionIsAdoptedAgain() {
    std::printf("A client's world replaced while it plays:\n");
    // A world loaded from a file, and one built in code, which a scene's epoch alone names as none.
    for (const bool fromFile : {true, false}) {
        const ScopedEngineSchema wire;
        SpawnMatch match(2);
        Scene first;
        buildCrates(fromFile ? first : match.clientWorld(), 2);  // the same world as the server's
        if (fromFile) match.clientWorld().swap(first);
        const char* joins = fromFile ? "joined, its world read off a file" : "joined, its world made in code";
        check(joins, match.join());

        Scene fresh;
        buildCrates(fresh, 2);
        match.clientWorld().swap(fresh);
        const EntityId crate = match.serverWorld().entityAt(match.clientWorld().entityAt(1).slot());
        match.serverWorld().get<Transform>(crate).position.y = 9.0f;
        const auto frame = [&]() { match.frame(); };
        const auto moved = [&]() {
            const Transform* at = match.clientWorld().tryGet<Transform>(match.mirrorOf(crate));
            return at && at->position.y > 8.0f;
        };
        check("  what the server says of a crate reaches the new world", pumpUntil(frame, moved, 80));
        int crates = 0;
        match.clientWorld().forEach<Rigidbody>([&](EntityId, const Rigidbody&) { ++crates; });
        check("  its own crate, not a second one beside it", crates == 2);
    }

    // Replaced before any session, after the host told the session its world: the next join
    // shares the world as it is then, not the one it was told.
    // The world it was told lacks a crate the world it shares has, at a slot of its own.
    const ScopedEngineSchema wire;
    SpawnMatch match(3);
    Scene told;
    buildCrates(told, 2);
    match.clientWorld().swap(told);
    match.client().setWorld(0, match.clientWorld());
    match.frame();
    Scene loaded;
    buildCrates(loaded, 3);
    match.clientWorld().swap(loaded);
    match.frame();
    check("joined after its world was replaced offline", match.joinTheWorldItWasTold());
    for (int i = 0; i < 30; ++i) match.frame();
    int crates = 0;
    match.clientWorld().forEach<Rigidbody>([&](EntityId, const Rigidbody&) { ++crates; });
    check("  the server's crates are the world's own, not built beside them", crates == 3);
}

// A client leaves and joins again: what the server made is built once, not beside a leftover of
// the session before.
void testAClientJoiningAgainBuildsTheServersThingsOnce() {
    std::printf("A client that leaves and joins again:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    const std::string path = scratchPrefab("net_spawn_rejoin_test.json");
    check("a prefab is written", writeSpawnablePrefab(path, match.resources(), 2));
    check("the client joins", match.join());
    const EntityId spawned = match.server().spawn(match.serverWorld(), match.resources(), path, Transform{});
    const auto frame = [&]() { match.frame(); };
    const auto built = [&]() { return match.isBuilt(spawned, 2); };
    check("the spawn is built on the client", pumpUntil(frame, built, 80));
    const EntityId first = match.mirrorOf(spawned);

    match.leave();
    check("leaving, the session's spawn leaves the client", !match.clientWorld().isAlive(first));

    check("it joins again", match.join());
    check("  the spawn built again", pumpUntil(frame, built, 80));
    int copies = 0;
    match.clientWorld().forEach<Name>([&](EntityId, const Name& name) {
        copies += std::string_view(name.value) == "SpawnedThing" ? 1 : 0;
    });
    check("  once", copies == 1);
}

// A client makes entities of its own while it plays - a menu, an effect - in whatever slot is
// free, and the server spawns into whatever slot it has: neither ever writes over the other.
void testAClientsOwnEntityIsNeverWrittenOver() {
    std::printf("An entity of the client's own, where the server spawns:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);

    const std::string path = scratchPrefab("net_spawn_beside_test.json");
    check("a prefab is written", writeSpawnablePrefab(path, match.resources(), 2));
    check("the client joins", match.join());

    // The server spawns; before a word of it reaches the client, the client has made something
    // of its own in that very slot.
    const EntityId spawned = match.server().spawn(match.serverWorld(), match.resources(), path, Transform{});
    const EntityId own     = match.clientWorld().createEntityAt(spawned.slot());
    check("the client holds an entity of its own in the slot the server spawned into", bool(own));
    match.clientWorld().add(own, makeName("Menu"));
    match.clientWorld().add(own, Transform{});

    const auto frame = [&]() { match.frame(); };
    const auto built = [&]() { return match.isBuilt(spawned, 2); };
    check("the spawn is built on the client", pumpUntil(frame, built, 80));
    check("  beside the client's own, not into it", match.mirrorOf(spawned) != own);
    check(
        "the client's own is untouched",
        match.clientWorld().isAlive(own) && !match.clientWorld().has<PrefabInstance>(own)
            && std::string_view(match.clientWorld().get<Name>(own).value) == "Menu"
    );
    check(
        "the spawn runs on the server, the client's own here",
        !match.client().simulates(match.clientWorld(), match.mirrorOf(spawned))
            && match.client().simulates(match.clientWorld(), own)
    );

    match.serverWorld().get<Transform>(spawned).position = {4.0f, 0.0f, 0.0f};
    const auto moved = [&]() {
        const EntityId mirror = match.mirrorOf(spawned);
        return match.clientWorld().isAlive(mirror)
            && std::abs(match.clientWorld().get<Transform>(mirror).position.x - 4.0f) < 0.01f;
    };
    check("what the server says of its spawn reaches the spawn", pumpUntil(frame, moved, 80));
    check("  and not the client's own", std::abs(match.clientWorld().get<Transform>(own).position.x) < 1e-6f);

    HierarchyOperations::destroyHierarchy(match.serverWorld(), spawned);
    const EntityId mirror = match.mirrorOf(spawned);
    check(
        "the server's spawn leaves the client when it goes",
        pumpUntil(frame, [&]() { return !match.clientWorld().isAlive(mirror); }, 80)
    );
    check("  and the client's own stays", match.clientWorld().isAlive(own));
}

// A joiner loads the scene file, which still holds what the server destroyed of it before the
// joiner came: told of nothing, the joiner would keep a crate no one else sees.
void testAJoinerLosesWhatTheWorldLostBeforeItCame() {
    std::printf("A player joining after part of the scene was destroyed:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(3);
    match.frame();  // the server takes the world as loaded

    // The joiner's copy of the scene, slot for slot.
    const std::vector<EntityId> loaded = buildCrates(match.clientWorld(), 3);
    match.serverWorld().destroyEntity(match.serverWorld().entityAt(loaded[1].slot()));
    check("the client joins", match.join());

    const auto frame = [&]() { match.frame(); };
    check(
        "the crate destroyed before it came goes from it too",
        pumpUntil(frame, [&]() { return !match.clientWorld().isAlive(loaded[1]); }, 80)
    );
    check(
        "  and the rest of the scene stays",
        match.clientWorld().isAlive(loaded[0]) && match.clientWorld().isAlive(loaded[2])
    );
}

// A server that loads another scene mid-session takes its players along: each is spawned again in
// it, told which scene to load, and - once it has loaded the same - plays on in it as the player it
// was, with nothing of the old world read into the new.
void testAServerTakesItsPlayersToTheNextScene() {
    std::printf("A server moving its session to another scene:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());
    const PlayerId player = match.client().localPlayer();

    // The next scene, as both ends load it: three crates.
    Scene next;
    buildCrates(next, 3);
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");

    const auto frame = [&]() { match.frame(); };
    std::string asked;
    const auto told = [&]() {
        asked = match.client().takeSceneToLoad();
        return !asked.empty();
    };
    check("the client is told which scene to load", pumpUntil(frame, told, 80));
    check("  the one the server moved to", asked == "scenes/next.json");
    // Gone from the new scene before the client has said what it holds of it.
    const uint32_t gone = match.serverWorld().entityAt(2).slot();
    match.serverWorld().destroyEntity(match.serverWorld().entityAt(gone));

    Scene loaded;
    buildCrates(loaded, 3);
    match.clientWorld().swap(loaded);
    match.client().setWorld(0x5EED, match.clientWorld(), asked);

    const EntityId crate = match.serverWorld().entityAt(3);
    match.serverWorld().get<Transform>(crate).position.x = 9.0f;
    const auto arrived = [&]() {
        const EntityId mirror = match.clientWorld().entityAt(3);
        return match.clientWorld().isAlive(mirror)
            && std::abs(match.clientWorld().get<Transform>(mirror).position.x - 9.0f) < 0.01f;
    };
    check("it plays on in the new scene, the server's word reaching it", pumpUntil(frame, arrived, 120));
    check("  as the player it was", match.client().isPlaying() && match.client().localPlayer() == player);
    check(
        "  driving the entity the server spawned it in the new world",
        match.clientWorld().isAlive(match.client().localEntity())
    );
    const auto ghost = [&]() { return !match.clientWorld().isAlive(match.clientWorld().entityAt(gone)); };
    check("  and what the server took from that scene goes from it too", pumpUntil(frame, ghost, 80));
}

// A server runs a player's commands on the entity it spawned that player in the world they were
// made for: none queued when it moves on, nor any sent from the old world while the next loads.
void testAServerRunsNoCommandMadeForTheWorldBefore() {
    std::printf("A player's commands across a change of scene:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    EntityId spawned{};
    match.server().onSpawn(
        [&spawned](Scene& scene, ResourceManager&, PlayerId) { return spawned = scene.createEntity(); },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId e) { scene.destroyEntity(e); }
    );
    buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());

    uint32_t tick     = 1000;
    uint32_t sequence = 1000;
    const auto walk = [&]() {
        InputCommand walking;
        walking.sequence = ++sequence;
        walking.tick     = ++tick;
        walking.axis[0]  = 1.0f;
        match.client().beginTick(tick, walking, 3, TEST_ACTION_NAMES);
        match.client().send(match.clientWorld(), tick);
        match.client().advance(1.0f / 60.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    // The server's step: what arrived, then the tick its players' commands run on.
    const auto step = [&]() {
        match.server().receive(match.serverWorld(), match.resources());
        match.server().beginTick(tick, InputCommand{}, 3, TEST_ACTION_NAMES);
        const float ran = match.server().commandFor(spawned).axis[0];
        match.server().send(match.serverWorld(), tick);
        match.server().advance(1.0f / 60.0f);
        match.client().receive(match.clientWorld(), match.resources());
        return ran;
    };

    for (int i = 0; i < 4; ++i) walk();
    match.server().receive(match.serverWorld(), match.resources());  // queued, not yet run
    Scene next;
    buildCrates(next, 3);
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    float walked = step();
    check("the next scene's player is spawned", match.serverWorld().isAlive(spawned));
    for (int i = 0; i < 20; ++i) {
        walk();
        walked = std::max(walked, step());
    }
    check("  and runs nothing the old world's player was told to do", walked == 0.0f);
}

// A scene change spawns every player again, and the old world's entities went with it: in the next,
// no one drives anything until its spawn says so, which is how a spawn finds what is free.
void testASpawnSeesWhoDrivesWhatInTheNextScene() {
    std::printf("Who drives what, as a scene change spawns its players:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    NetSession& server = match.server();
    int declined = 0;
    server.onSpawn(
        [&](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId first = scene.entityAt(1);
            if (!server.isDriven(first)) return first;
            ++declined;
            return EntityId{};
        },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());
    check("the crate it was seated at is driven", server.isDriven(match.serverWorld().entityAt(1)));
    check("  and the other is not", !server.isDriven(match.serverWorld().entityAt(2)));
    check("  which only the server is asked", !match.client().isDriven(match.client().localEntity()));

    Scene next;
    buildCrates(next, 2);
    match.serverWorld().swap(next);
    server.setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    match.frame();
    check("in the next scene, the first crate is free for it again", declined == 0);
    check("  and driven once it is seated there", server.isDriven(match.serverWorld().entityAt(1)));
}

// A player joining after the server moved on holds the scene it entered, not the server's: the
// entity it is given is in the server's, so it drives nothing until it has loaded that.
void testALateJoinerDrivesNothingInTheSceneItLeaves() {
    std::printf("A player joining a session that moved to another scene:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    match.frame();  // the server takes its first world as loaded
    Scene next;
    buildCrates(next, 3);
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    match.frame();

    buildCrates(match.clientWorld(), 2);
    match.client().setWorld(0xE7, match.clientWorld(), "scenes/entry.json");
    match.client().connect(NetAddress{0x7F000001u, match.server().localAddress().port}, TEST_TICK_RATE);
    const auto frame = [&]() { match.frame(); };
    check("it is seated", pumpUntil(frame, [&]() { return match.client().isPlaying(); }));
    check("  driving nothing in the scene it entered", !match.client().localEntity());

    std::string asked;
    const auto told = [&]() {
        asked = match.client().takeSceneToLoad();
        return !asked.empty();
    };
    check("it is told which scene to load", pumpUntil(frame, told, 80));
    Scene loaded;
    buildCrates(loaded, 3);
    match.clientWorld().swap(loaded);
    match.client().setWorld(0x5EED, match.clientWorld(), asked);
    const auto driving = [&]() { return match.clientWorld().isAlive(match.client().localEntity()); };
    check("  and drives its entity there once loaded", pumpUntil(frame, driving, 80));
}

// The session's world number is a byte: past its last value it must not read as the first world,
// which refuses a joiner whose entry scene is not the server's.
void testASessionThatMovedOnOftenStillSeatsALateJoiner() {
    std::printf("A player joining after the session changed scene a byte's worth of times:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    match.frame();  // the server takes its first world as loaded
    for (int change = 0; change < 256; ++change) {
        Scene next;
        buildCrates(next, 1);
        match.serverWorld().swap(next);
        match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
        match.frame();
    }

    buildCrates(match.clientWorld(), 2);
    match.client().setWorld(0xE7, match.clientWorld(), "scenes/entry.json");
    match.client().connect(NetAddress{0x7F000001u, match.server().localAddress().port}, TEST_TICK_RATE);
    const auto frame = [&]() { match.frame(); };
    check("it is seated", pumpUntil(frame, [&]() { return match.client().isPlaying(); }));
}

// A load can hold a client's frames, and every packet it would send with them: a player loading
// the scene the server moved to is given longer than a silent one.
void testAPlayerLoadingTheNextSceneIsWaitedFor() {
    std::printf("A player silent while it loads the next scene:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());
    // Only the server's frames: the client is held in its load.
    const auto silence = [&](float seconds) {
        for (float waited = 0.0f; waited < seconds; waited += NetConnection::MAX_SILENCE_A_FRAME) {
            match.server().receive(match.serverWorld(), match.resources());
            match.server().send(match.serverWorld(), 1);
            match.server().advance(NetConnection::MAX_SILENCE_A_FRAME);
        }
    };

    Scene next;
    buildCrates(next, 3);
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    silence(NetConnection::TIMEOUT_SECONDS + 3.0f);
    check("past the timeout, it is still seated", match.server().playerCount() == 1);

    std::string asked;
    const auto frame = [&]() { match.frame(); };
    const auto told  = [&]() {
        asked = match.client().takeSceneToLoad();
        return !asked.empty();
    };
    check("it is told which scene to load", pumpUntil(frame, told, 80));
    Scene loaded;
    buildCrates(loaded, 3);
    match.clientWorld().swap(loaded);
    match.client().setWorld(0x5EED, match.clientWorld(), asked);
    const auto driving = [&]() { return match.clientWorld().isAlive(match.client().localEntity()); };
    check("  and plays on once it has loaded it", pumpUntil(frame, driving, 80));
    silence(NetConnection::TIMEOUT_SECONDS + 3.0f);
    check("silent as long once it holds the scene, it is let go", match.server().playerCount() == 0);
}

// The next scene's entities can carry the old one's ids, so every player is unseated before any is
// spawned again: the first spawn sees nothing driven, not the other player's place in the old world.
void testEveryPlayerIsUnseatedBeforeTheNextSceneSpawnsAny() {
    std::printf("Two players taken to the next scene:\n");

    const ScopedEngineSchema wire;
    const ScratchProject     project("vkm_net_reseat");
    ResourceManager          resources;
    Scene                    serverWorld;
    buildCrates(serverWorld, 2);
    NetSession server;
    int  spawns    = 0;
    bool sawDriven = false;
    server.onSpawn(
        [&](Scene& scene, ResourceManager&, PlayerId) {
            if (spawns++ == 0) {
                sawDriven = server.isDriven(scene.entityAt(1)) || server.isDriven(scene.entityAt(2));
            }
            for (const uint32_t slot : {1u, 2u}) {
                if (!server.isDriven(scene.entityAt(slot))) return scene.entityAt(slot);
            }
            return EntityId{};
        },
        [](Scene&, ResourceManager&, PlayerId, EntityId) {}
    );
    server.host(0, 4, TEST_TICK_RATE);

    Scene      worlds[2];
    NetSession clients[2];
    for (int c = 0; c < 2; ++c) {
        buildCrates(worlds[c], 2);
        clients[c].setWorld(0, worlds[c]);
        clients[c].connect(NetAddress{0x7F000001u, server.localAddress().port}, TEST_TICK_RATE);
    }
    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        for (int c = 0; c < 2; ++c) clients[c].receive(worlds[c], resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3, TEST_ACTION_NAMES);
        for (int c = 0; c < 2; ++c) clients[c].beginTick(tick, InputCommand{}, 3, TEST_ACTION_NAMES);
        server.send(serverWorld, tick);
        for (int c = 0; c < 2; ++c) clients[c].send(worlds[c], tick);
        server.advance(1.0f / 60.0f);
        for (int c = 0; c < 2; ++c) clients[c].advance(1.0f / 60.0f);
    };
    const auto seated = [&]() { return clients[0].isPlaying() && clients[1].isPlaying(); };
    const auto bothDriven = [&]() {
        return server.isDriven(serverWorld.entityAt(1)) && server.isDriven(serverWorld.entityAt(2));
    };
    check("both join", pumpUntil(frame, seated));
    check("  each on a crate of its own", bothDriven());

    Scene next;
    buildCrates(next, 2);
    serverWorld.swap(next);
    server.setWorld(0x5EED, serverWorld, "scenes/next.json");
    spawns = 0;
    frame();
    check("the next scene's first spawn sees nothing driven", spawns == 2 && !sawDriven);
    check("  and both are seated there", bothDriven());

    for (NetSession& client : clients) client.close();
    server.close();
}

// A world replaced in the frame is described only once the session has taken it in: sent before,
// it would go out under the old world's number, into the old world at every client.
void testAWorldReplacedThisFrameIsNotSentAsTheOld() {
    std::printf("A server sending in the frame it replaced its world:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    const std::vector<EntityId> held = buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());

    Scene next;
    const std::vector<EntityId> crates = buildCrates(next, 2);
    next.get<Transform>(crates[0]).position.x = 50.0f;
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    match.server().advance(1.0f);
    match.server().send(match.serverWorld(), 1000);

    for (int i = 0; i < 20; ++i) {
        match.client().receive(match.clientWorld(), match.resources());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const EntityId mine = match.clientWorld().entityAt(held[0].slot());
    const bool kept = match.clientWorld().isAlive(mine)
        && match.clientWorld().get<Transform>(mine).position.x < 10.0f;
    check("the client's world is not written with the next one's", kept);
}

// A world the server moves to that does not load here: the client cannot play on in the world it
// left, so it leaves, and says why.
void testAClientThatCannotLoadTheNextWorldLeaves() {
    std::printf("A client the server moves to a world it cannot load:\n");

    const ScopedEngineSchema wire;
    SpawnMatch match(2);
    buildCrates(match.clientWorld(), 2);
    check("the client joins", match.join());

    Scene next;
    buildCrates(next, 3);
    match.serverWorld().swap(next);
    match.server().setWorld(0x5EED, match.serverWorld(), "scenes/next.json");
    const auto frame = [&]() { match.frame(); };
    const auto told  = [&]() { return !match.client().takeSceneToLoad().empty(); };
    check("the client is told which scene to load", pumpUntil(frame, told, 80));

    match.client().worldFailed();
    check("failing to load it, it leaves", !match.client().isPlaying());
    check("  saying why", !match.client().lastError().empty());
}

// A component the server takes away goes from every client too, and one put back comes back.
void testAComponentTakenAwayIsTakenAwayEverywhere() {
    std::printf("A component the server takes away:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    Scene client;
    NetNames clientNames;

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);
    baseline.confirm(1);
    const EntityId mirrored = client.entityAt(crate.slot());
    check("the client holds what the server does", client.has<Rigidbody>(mirrored));

    server.remove<Rigidbody>(crate);
    const NetSnapshotStats gone = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("taking it away is news", gone.entitiesWritten == 1);
    check("  that the client acts on", readSnapshotFrom(client, schema, body, clientNames));
    check("  taking it away too", !client.has<Rigidbody>(mirrored) && client.has<Transform>(mirrored));
    baseline.confirm(2);
    const NetSnapshotStats quiet = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("once confirmed, it is not said again", quiet.entitiesWritten == 0);

    server.add(crate, Rigidbody{});
    writeSnapshotTo(server, schema, 4, budget, baseline, body);
    readSnapshotFrom(client, schema, body, clientNames);
    check("put back, it comes back", client.has<Rigidbody>(mirrored));
}

// A late acknowledgement of a snapshot from before a removal must not have the server believe the
// client holds the component again, or putting it back as it was would never be sent.
void testALateAcknowledgementDoesNotBringARemovalBack() {
    std::printf("An old acknowledgement arriving after a removal's:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshotTo(server, schema, 9, budget, baseline, body);
    baseline.confirm(9);
    server.get<Rigidbody>(crate).linearVelocity = {1.0f, 0.0f, 0.0f};
    writeSnapshotTo(server, schema, 10, budget, baseline, body);  // the new velocity, unconfirmed
    const Rigidbody moving = server.get<Rigidbody>(crate);
    server.remove<Rigidbody>(crate);
    writeSnapshotTo(server, schema, 11, budget, baseline, body);  // said gone
    baseline.confirm(11);
    baseline.confirm(10);  // late, and older than the removal

    server.add(crate, moving);
    const NetSnapshotStats back = writeSnapshotTo(server, schema, 12, budget, baseline, body);
    check("the component put back is still sent", back.componentsWritten == 1);
}

// Both ends load the same scene. A joiner's first snapshot describes every entity, but one the server
// has not moved is the one the client already holds exactly: its quantised copy would only shift it.
void testAnAuthoredPoseIsKeptExactOnJoin() {
    std::printf("A joiner's world, as its scene file wrote it:\n");

    NetSchema schema;
    registerEngineNetTypes(schema);
    const glm::vec3 AUTHORED{1.2345678f, 2.2f, -3.3333333f};
    const auto build = [&](Scene& world) {
        const std::vector<EntityId> crates = buildCrates(world, 2);
        for (const EntityId crate : crates) world.get<Transform>(crate).position = AUTHORED;
        return crates;
    };
    Scene server;
    Scene client;
    const std::vector<EntityId> crates = build(server);
    build(client);
    server.get<Transform>(crates[1]).position.x += 5.0f;

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;
    NetNames clientNames;
    clientNames.adoptWorld(client);
    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    check("the whole world is described", readSnapshotFrom(client, schema, body, clientNames));

    const glm::vec3 kept  = client.get<Transform>(client.entityAt(crates[0].slot())).position;
    const glm::vec3 moved = client.get<Transform>(client.entityAt(crates[1].slot())).position;
    check("an entity the server has not moved keeps its exact pose", kept == AUTHORED);
    check("  and one it moved takes the server's", std::abs(moved.x - (AUTHORED.x + 5.0f)) < 0.002f);
}

} // namespace

void runNetReplicationTests() {
    testTheFirstSnapshotIsTheWholeWorld();
    testALostSnapshotIsSaidAgain();
    testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne();
    testAConfirmationAWholeTurnLaterIsStillNewer();
    testAnOwnerTooLargeForASnapshotLeavesTheRest();
    testALateWordOfAnOldOccupantIsMoot();
    testALateWordOfAnEmptiedSlotIsMoot();
    testABurstIsDeferredRatherThanDropped();
    testAnEntityThatLeavesTheWorldLeavesEveryClient();
    testASlotReusedBetweenTwoSnapshotsStillReportsWhatLeft();
    testASnapshotThatWasNeverSentClaimsNothing();
    testASnapshotNeverOutgrowsThePacketItRidesIn();
    testABoneTheAnimationPlacesIsNotWorthAPacket();
    testASpawnRefusesWhatItCannotMean();
    testWhatASpawnedRootIsGoesOutWhenItsStateDoesNot();
    testSomethingBuiltAfterTheSceneLoadedReachesEveryone();
    testABurstOfSpawnsArrivesBuiltRatherThanBare();
    testAPlayerWhoJoinsLateSeesWhatWasSpawned();
    testASpawnedStaticBodyKeepsItsExactPose();
    testWhatEachEndRunsItselfIsNeverSent();
    testASlotSpawnedIntoAgainBuildsTheNewThing();
    testAClientsOwnEntityIsNeverWrittenOver();
    testAJoinerLosesWhatTheWorldLostBeforeItCame();
    testAServerTakesItsPlayersToTheNextScene();
    testAServerRunsNoCommandMadeForTheWorldBefore();
    testASpawnSeesWhoDrivesWhatInTheNextScene();
    testEveryPlayerIsUnseatedBeforeTheNextSceneSpawnsAny();
    testALateJoinerDrivesNothingInTheSceneItLeaves();
    testASessionThatMovedOnOftenStillSeatsALateJoiner();
    testAPlayerLoadingTheNextSceneIsWaitedFor();
    testAClientThatCannotLoadTheNextWorldLeaves();
    testAWorldReplacedThisFrameIsNotSentAsTheOld();
    testAComponentTakenAwayIsTakenAwayEverywhere();
    testALateAcknowledgementDoesNotBringARemovalBack();
    testAnAuthoredPoseIsKeptExactOnJoin();
    testAClientJoiningAgainBuildsTheServersThingsOnce();
    testAWorldReplacedMidSessionIsAdoptedAgain();
}
