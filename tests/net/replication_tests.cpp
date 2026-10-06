#include "physics/physics_support.h"
#include "net/net_support.h"

#include <algorithm>

#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "net/net_session.h"
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
    check("and the client takes it", readSnapshotFrom(client, schema, body));
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
    readSnapshotFrom(client, schema, body);

    // The crate moves, that snapshot is lost, then it stops. A server that folded the
    // snapshot in when writing it would leave the crate at the old position for good.
    server.get<Transform>(crate).position = {99.0f, 1.0f, 2.0f};
    const NetSnapshotStats moved = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("the move is written", moved.entitiesWritten == 1);

    // Snapshot 2 is never confirmed - it did not arrive.
    const NetSnapshotStats again = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("and while it is unconfirmed the move is written again", again.entitiesWritten == 1);

    readSnapshotFrom(client, schema, body);
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
    uint16_t sequence = 1;
    int snapshots = 0;
    uint32_t worst = 0;

    // Until every body has arrived, confirming each snapshot.
    while (snapshots < 20) {
        const NetSnapshotStats stats = writeSnapshotTo(server, schema, sequence, budget, baseline, body);
        worst = std::max(worst, stats.bytesWritten);
        check("no snapshot exceeds what a datagram can carry", stats.bytesWritten <= budget.bytes);
        readSnapshotFrom(client, schema, body);
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
    readSnapshotFrom(probe, schema, body);
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
    readSnapshotFrom(client, schema, body);
    check("six crates to start", client.entityCount() == 6);

    const uint32_t goneSlot = crates[2].slot();
    server.destroyEntity(crates[2]);
    const NetSnapshotStats told = writeSnapshotTo(server, schema, 2, budget, baseline, body);
    check("the server reports the one that went", told.entitiesLost == 1);

    // Lost before confirmed, so reported again: destruction must be reliable, or the
    // client keeps a body nobody else sees.
    const NetSnapshotStats retold = writeSnapshotTo(server, schema, 3, budget, baseline, body);
    check("and keeps reporting it until the client confirms", retold.entitiesLost == 1);

    readSnapshotFrom(client, schema, body);
    baseline.confirm(3);
    check("the client destroyed it", client.entityCount() == 5);
    check("and it was the right one", !client.isAliveAtIndex(goneSlot));

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

    writeSnapshotTo(server, schema, 1, budget, baseline, body);
    readSnapshotFrom(client, schema, body);
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
    readSnapshotFrom(client, schema, body);
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
    check("the client takes it", readSnapshotFrom(client, schema, body));
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
    tooLong.prefab.assign(NET_PREFAB_PATH_MAX + 1, 'x');
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
    readSnapshotFrom(client, schema, body);
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
            m_client.connect(NetAddress{0x7F000001u, m_server.localAddress().port}, TEST_TICK_RATE);
            const size_t world = m_serverWorld.entityCount();
            return pumpUntil([&]() { frame(); }, [&]() { return m_client.isPlaying(); })
                && pumpUntil([&]() { frame(); }, [&]() { return m_clientWorld.entityCount() >= world; }, 80);
        }

        /// One frame of both ends, in a host's order.
        void frame() {
            m_server.receive(m_serverWorld, m_resources);
            m_client.receive(m_clientWorld, m_resources);
            ++m_tick;
            m_server.beginTick(m_tick, InputCommand{}, 3);
            m_client.beginTick(m_tick, InputCommand{}, 3);
            m_server.send(m_serverWorld, m_tick);
            m_client.send(m_clientWorld, m_tick);
            m_server.advance(1.0f / 60.0f);
            m_client.advance(1.0f / 60.0f);
        }

        /// Whether @p root has arrived at the client built from its prefab.
        bool isBuilt(EntityId root, int parts) const {
            if (!m_clientWorld.isAliveAtIndex(root.slot())) return false;
            const EntityId mirror = m_clientWorld.entityAt(root.slot());
            return m_clientWorld.has<PrefabInstance>(mirror) && childrenOf(m_clientWorld, mirror) == parts;
        }

    public:
        ResourceManager& resources()   { return m_resources; }
        Scene&           serverWorld() { return m_serverWorld; }
        Scene&           clientWorld() { return m_clientWorld; }
        NetSession&      server()      { return m_server; }

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
        const EntityId mirror = match.clientWorld().entityAt(second.slot());
        return match.clientWorld().get<PrefabInstance>(mirror).source == secondPath;
    };
    check("and the client ends holding the second, not the first", pumpUntil(frame, isSecond, 80));
}

} // namespace

void runNetReplicationTests() {
    testTheFirstSnapshotIsTheWholeWorld();
    testALostSnapshotIsSaidAgain();
    testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne();
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
    testASlotSpawnedIntoAgainBuildsTheNewThing();
}
