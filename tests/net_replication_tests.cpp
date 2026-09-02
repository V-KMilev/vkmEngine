#include "support.h"

namespace {

void testTheFirstSnapshotIsTheWholeWorld() {
    std::printf("Joining and playing are the same code path:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 40);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    const NetSnapshotStats first = writeSnapshot(server, schema, 1, budget, baseline, body);
    check("a connection that has confirmed nothing is sent everything",
          first.entitiesWritten == 40);
    check("with no separate mode for it", first.entitiesDeferred == 0);

    Scene client;
    check("and the client takes it", readSnapshotFrom(client, schema, body));
    check("building every entity it had never heard of", client.entityCount() == 40);

    const EntityId last = client.entityAt(crates.back().slot());
    check("at the position the server had it",
          std::abs(client.get<Transform>(last).position.x - 39 * 1.5f) < 0.002f);

    // Nothing moved, and the client confirmed. The second snapshot must be
    // silent: this is what makes a settled world of any size free, and without
    // it a tower of crates costs a packet a tick forever after it stops.
    baseline.confirm(1);
    const NetSnapshotStats second = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("a world that has not changed sends nothing", second.entitiesWritten == 0);
    std::printf("      whole world %u B, then %u B once it settled\n",
                first.bytesWritten, second.bytesWritten);
}

void testALostSnapshotIsSaidAgain() {
    std::printf("The bug that freezes a body on one client for the rest of a match:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 4).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshot(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);

    Scene client;
    readSnapshotFrom(client, schema, body);

    // The crate moves, the snapshot saying so is lost, then it stops. Nothing
    // about it differs again - so a server that folded that snapshot in when it
    // wrote it leaves the crate at the old position until the match ends.
    server.get<Transform>(crate).position = {99.0f, 1.0f, 2.0f};
    const NetSnapshotStats moved = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("the move is written", moved.entitiesWritten == 1);

    // Snapshot 2 is never confirmed - it did not arrive.
    const NetSnapshotStats again = writeSnapshot(server, schema, 3, budget, baseline, body);
    check("and while it is unconfirmed the move is written again",
          again.entitiesWritten == 1);

    readSnapshotFrom(client, schema, body);
    baseline.confirm(3);
    check("so the client ends up where the server put it",
          std::abs(client.get<Transform>(client.entityAt(crate.slot())).position.x - 99.0f) < 0.002f);

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 4, budget, baseline, body);
    check("and only then does the server stop saying it", quiet.entitiesWritten == 0);
}

void testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne() {
    std::printf("Acknowledgements arrive in whatever order the network chose:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const EntityId crate = buildCrates(server, 1).front();

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    server.get<Transform>(crate).position = {1.0f, 0.0f, 0.0f};
    writeSnapshot(server, schema, 10, budget, baseline, body);
    server.get<Transform>(crate).position = {2.0f, 0.0f, 0.0f};
    writeSnapshot(server, schema, 11, budget, baseline, body);

    // The newer arrives first, then the older. Folding the older in afterwards
    // would leave the server believing the client holds x=1, and it would then
    // resend x=2 forever - or worse, believe x=2 was already delivered.
    baseline.confirm(11);
    baseline.confirm(10);

    const NetSnapshotStats after = writeSnapshot(server, schema, 12, budget, baseline, body);
    check("the older acknowledgement does not overwrite the newer",
          after.entitiesWritten == 0);
}

void testABurstIsDeferredRatherThanDropped() {
    std::printf("What a collapsing tower does to a packet:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 117);

    NetBaseline baseline;
    NetBudget budget;
    budget.owner = crates.front();
    std::vector<uint8_t> body;

    // Every body awake and moving - the frame the old attempt measured at 8455
    // bytes and 643 consecutive refusals, where the client received nothing at
    // all at the exact moment there was most to see.
    server.forEachEntity([&](EntityId entity) {
        server.get<Rigidbody>(entity).linearVelocity  = {3.0f, -9.0f, 1.5f};
        server.get<Rigidbody>(entity).angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    Scene client;
    uint16_t sequence = 1;
    int snapshots = 0;
    uint32_t worst = 0;

    // Drive it until every body has arrived, confirming each snapshot.
    while (snapshots < 20) {
        const NetSnapshotStats stats = writeSnapshot(server, schema, sequence, budget, baseline, body);
        worst = std::max(worst, stats.bytesWritten);
        check("no snapshot exceeds what a datagram can carry", stats.bytesWritten <= budget.bytes);
        readSnapshotFrom(client, schema, body);
        baseline.confirm(sequence);
        ++sequence;
        ++snapshots;
        // The connection's own entity is written every snapshot whether it
        // moved or not, so "nothing to say" is one entry rather than none.
        if (stats.entitiesWritten <= 1) break;
    }

    check("every body reached the client", client.entityCount() == 117);
    check("and it took a handful of snapshots, not a hundred", snapshots <= 6);
    std::printf("      117 moving bodies delivered in %d snapshots, worst %u B\n",
                snapshots, worst);

    // The owner is the anchor reconciliation compares against, so it must never
    // be the entity that gets deferred.
    NetBaseline fresh;
    const NetSnapshotStats first = writeSnapshot(server, schema, 100, budget, fresh, body);
    check("the burst does not fit one snapshot", first.entitiesDeferred > 0);

    bool ownerPresent = false;
    Scene probe;
    readSnapshotFrom(probe, schema, body);
    ownerPresent = probe.isAliveAtIndex(budget.owner.slot());
    check("and the owner is in it anyway", ownerPresent);
}

void testAnEntityThatLeavesTheWorldLeavesEveryClient() {
    std::printf("What a client is told about an entity that is gone:\n");

    NetSchema schema;
    fillWorldSchema(schema);
    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 6);

    NetBaseline baseline;
    NetBudget budget;
    std::vector<uint8_t> body;

    writeSnapshot(server, schema, 1, budget, baseline, body);
    baseline.confirm(1);
    Scene client;
    readSnapshotFrom(client, schema, body);
    check("six crates to start", client.entityCount() == 6);

    const uint32_t goneSlot = crates[2].slot();
    server.destroyEntity(crates[2]);
    const NetSnapshotStats told = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("the server reports the one that went", told.entitiesLost == 1);

    // Lost before it was confirmed, so it must be reported again - destruction
    // has to be reliable or the client keeps a body nobody else can see.
    const NetSnapshotStats retold = writeSnapshot(server, schema, 3, budget, baseline, body);
    check("and keeps reporting it until the client confirms", retold.entitiesLost == 1);

    readSnapshotFrom(client, schema, body);
    baseline.confirm(3);
    check("the client destroyed it", client.entityCount() == 5);
    check("and it was the right one", !client.isAliveAtIndex(goneSlot));

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 4, budget, baseline, body);
    check("only then does the server stop mentioning it", quiet.entitiesLost == 0);
}

void testOwnerOnlyStateGoesToItsOwnerAlone() {
    std::printf("State that exists to make one player's prediction converge:\n");

    NetSchema schema;
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody", NetPolicy::OwnerOnly);

    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 3);
    server.forEachEntity([&](EntityId entity) {
        server.get<Rigidbody>(entity).linearVelocity = {5.0f, 0.0f, 0.0f};
    });

    std::vector<uint8_t> body;
    NetBaseline mine, theirs;
    NetBudget forOwner;
    forOwner.owner = crates[1];
    NetBudget forOther;
    forOther.owner = crates[0];

    writeSnapshot(server, schema, 1, forOwner, mine, body);
    Scene ownerView;
    readSnapshotFrom(ownerView, schema, body);

    writeSnapshot(server, schema, 1, forOther, theirs, body);
    Scene otherView;
    readSnapshotFrom(otherView, schema, body);

    const uint32_t ownedSlot = crates[1].slot();
    check("the owner is told its own body's velocity",
          ownerView.has<Rigidbody>(ownerView.entityAt(ownedSlot)));
    check("and nobody else is",
          !otherView.has<Rigidbody>(otherView.entityAt(ownedSlot)));
    check("but everyone still sees where it is",
          std::abs(otherView.get<Transform>(otherView.entityAt(ownedSlot)).position.x - 1.5f) < 0.002f);
}

void testASnapshotNeverOutgrowsThePacketItRidesIn() {
    std::printf("The entry that is never deferred, and the budget it can still blow:\n");

    NetSchema schema;
    fillWorldSchema(schema);

    Scene server;
    const std::vector<EntityId> crates = buildCrates(server, 90);

    // Far up the slot range, past everything else. An id is a gap from the one
    // before it, and a gap this size costs three times a neighbouring one -
    // which is what matters, since the room is reserved before the gaps are known.
    const EntityId owner = server.createEntityAt(5000);
    Transform at;
    at.position = {40.0f, 4.0f, 0.0f};
    server.add(owner, at);
    server.add(owner, Rigidbody{});

    server.forEach<Rigidbody>([&](EntityId, Rigidbody& body) {
        body.linearVelocity  = {3.0f, -9.0f, 1.5f};
        body.angularVelocity = {0.0f, 2.0f, 0.0f};
    });

    // Every budget across the range where the world does not fit. The
    // connection's own entity is written wherever slot order puts it and is
    // never deferred, so on exactly the budgets where everything else has just
    // filled the packet, it is the one entry that can carry the body past the
    // end - and the datagram it rides in was sized from that same budget, so
    // the overflow has nowhere to go and the whole snapshot is lost. It arrives
    // as a header with nothing behind it, which reads as a packet that would
    // not decode: a symptom several steps from its cause.
    // The owner is last in slot order on purpose: everything else has spent the
    // budget by the time the write reaches it, which is the only arrangement
    // where the entry that cannot be deferred is also the one with no room.
    uint32_t over = 0;
    uint32_t worst = 0;
    uint32_t budgets = 0;
    for (uint32_t bytes = 40; bytes <= 1200; ++bytes) {
        NetBaseline baseline;
        NetBudget budget;
        budget.bytes = bytes;
        budget.owner = owner;
        ++budgets;

        std::vector<uint8_t> body;
        const NetSnapshotStats stats = writeSnapshot(server, schema, 1, budget, baseline, body);
        if (body.size() > bytes) {
            ++over;
            worst = std::max(worst, static_cast<uint32_t>(body.size()) - bytes);
        }
        check("the owner is in it whatever the budget", stats.entitiesWritten >= 1);
    }
    check("and no budget produces a body larger than itself", over == 0);
    std::printf("      %u budgets from 40 to 1200 bytes, %u over by up to %u byte(s)\n",
                budgets, over, worst);
}

void testABoneTheAnimationPlacesIsNotWorthAPacket() {
    std::printf("What a character costs when the animation already says it:\n");

    NetSchema schema;
    fillWorldSchema(schema);

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

    // Inactive: RagdollSystem places every bone from the animated pose, on both
    // ends, from a clip chosen by a velocity that does replicate. So the bones
    // are worked out rather than told, and none of them belongs on the wire.
    check("the ragdoll starts inactive", !server.get<Ragdoll>(rig).active);

    const NetSnapshotStats quiet = writeSnapshot(server, schema, 1, budget, baseline, body);
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the animation places is not described",
              isPosedByAnimation(server, bone.body));
    }
    const uint32_t withoutBones = quiet.entitiesWritten;
    std::printf("      inactive: %u entities on the wire, %u bones held back\n",
                withoutBones, bones);

    // Active: the solver drives the bones and the pose follows, so now they are
    // the answer rather than a copy and every one travels. A client that missed
    // the switch would pose a walk cycle over a body that is falling.
    server.get<Ragdoll>(rig).active = true;
    for (const RagdollBone& bone : server.get<Ragdoll>(rig).bones) {
        check("  a bone the solver drives is described",
              !isPosedByAnimation(server, bone.body));
    }

    const NetSnapshotStats loud = writeSnapshot(server, schema, 2, budget, baseline, body);
    check("switching physics on puts the whole skeleton on the wire",
          loud.entitiesWritten >= withoutBones + bones);
    std::printf("      active:   %u entities on the wire\n", loud.entitiesWritten);

    // And the switch itself travels, which is what makes the two states agree.
    baseline.confirm(2);
    Scene client;
    check("the client takes it", readSnapshotFrom(client, schema, body));
    const EntityId mirror = client.entityAt(rig.slot());
    check("the client learned the ragdoll is active",
          client.isAlive(mirror) && client.has<Ragdoll>(mirror)
          && client.get<Ragdoll>(mirror).active);
}

void testASpawnRefusesWhatItCannotMean() {
    std::printf("A spawn message, and the two values it must not carry:\n");

    NetSpawn spawn;
    spawn.slot        = 42;
    spawn.prefab      = "prefabs/crate.json";
    spawn.at.position = {3.0f, 4.0f, 5.0f};
    spawn.at.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    std::vector<uint8_t> bytes;
    BitWriter out(bytes, NetReliable::MAX_MESSAGE);
    check("an ordinary spawn is written", writeSpawn(out, spawn));
    out.finish();

    BitReader in(bytes.data(), bytes.size());
    check("  and its kind byte reads back", in.u8() == static_cast<uint8_t>(NetEvent::Spawn));

    NetSpawn back;
    check("  and it decodes", readSpawn(in, back));
    check("  to the same slot and path", back.slot == 42 && back.prefab == spawn.prefab);
    check("  at full precision, since nothing later corrects it",
          back.at.position.x == 3.0f && back.at.position.z == 5.0f);

    // A path the length field cannot describe. Written anyway, the length would
    // wrap and every byte after this message would be read at the wrong offset.
    NetSpawn tooLong = spawn;
    tooLong.prefab.assign(NET_PREFAB_PATH_MAX + 1, 'x');
    std::vector<uint8_t> refused;
    BitWriter out2(refused, NetReliable::MAX_MESSAGE);
    check("a path longer than the wire can name is refused", !writeSpawn(out2, tooLong));

    // The position is the one raw float in the protocol, so it is the one value
    // a sender can put anything at all into. A NaN reaches a Transform and
    // spreads to everything computed from it.
    std::vector<uint8_t> poisoned;
    BitWriter out3(poisoned, NetReliable::MAX_MESSAGE);
    check("a spawn is written to poison", writeSpawn(out3, spawn));
    out3.finish();

    // Overwrite the first coordinate in place: kind byte, slot, then the path.
    BitWriter forged(poisoned, NetReliable::MAX_MESSAGE);
    forged.u8(static_cast<uint8_t>(NetEvent::Spawn));
    forged.u32(spawn.slot);
    forged.bits(static_cast<uint32_t>(spawn.prefab.size()), 8);
    for (const char c : spawn.prefab) forged.bits(static_cast<uint8_t>(c), 8);
    forged.f32(std::numeric_limits<float>::quiet_NaN());
    forged.f32(4.0f);
    forged.f32(5.0f);
    Quantize::writeRotation(forged, spawn.at.rotation);
    forged.finish();

    BitReader poison(poisoned.data(), poisoned.size());
    check("the forged kind byte still reads", poison.u8() == static_cast<uint8_t>(NetEvent::Spawn));

    NetSpawn nowhere;
    check("a spawn at a position that is not a number is refused",
          !readSpawn(poison, nowhere));
}

void testSomethingBuiltAfterTheSceneLoadedReachesEveryone() {
    std::printf("A thing that did not exist when either end opened the world:\n");

    const ScopedEngineSchema wire;
    ResourceManager resources;

    // A prefab with a root and two children, written from a live subtree the
    // way the editor writes one.
    const char* const scratch = std::getenv("VKM_TEST_SCRATCH");
    const std::string prefabPath =
        (scratch ? std::filesystem::path(scratch) : std::filesystem::temp_directory_path())
        .append("net_spawn_test.prefab").string();
    {
        Scene authoring;
        const EntityId root = authoring.createEntity();
        Transform at;
        at.position = {1.0f, 2.0f, 3.0f};
        authoring.add(root, at);
        authoring.add(root, Rigidbody{});
        authoring.add(root, makeName("SpawnedThing"));
        for (int i = 0; i < 2; ++i) {
            const EntityId child = authoring.createEntity();
            Transform local;
            local.position = {static_cast<float>(i), 0.5f, 0.0f};
            authoring.add(child, local);
            authoring.add(child, makeName("Part"));
            HierarchyOperations::setParent(authoring, child, root);
        }
        check("a prefab is written from a live subtree",
              Prefab::save(authoring, root, prefabPath, resources));
    }

    Scene serverWorld;
    buildCrates(serverWorld, 5);
    NetSession server;
    server.onSpawn([](Scene& scene, ResourceManager&, PlayerId) { return scene.createEntity(); },
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
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };

    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }));
    check("and the authored world arrives",
          pumpUntil(frame, [&]() { return clientWorld.entityCount() >= 6; }, 80));

    // Now build something neither end had. This is the case the whole feature
    // exists for: a client told only where an entity is, having never been told
    // what it is, would draw nothing at all.
    Transform where;
    where.position = {-12.5f, 4.25f, 7.0f};
    const EntityId spawned = server.spawn(serverWorld, resources, prefabPath, where);
    check("the server builds it", serverWorld.isAlive(spawned));
    check("as a subtree, not one entity",
          childrenOf(serverWorld, spawned) == 2);

    check("and it reaches the client",
          pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(spawned.slot()); }, 80));

    const EntityId mirror = clientWorld.entityAt(spawned.slot());
    check("at the same slot, which is its name on the wire", clientWorld.isAlive(mirror));
    check("the subtree built from the same file, not sent over the wire",
          childrenOf(clientWorld, mirror) == 2);
    check("marked as an instance, so a later save writes the reference",
          clientWorld.has<PrefabInstance>(mirror));
    check("placed where the server placed it",
          glm::length(clientWorld.get<Transform>(mirror).position - where.position) < 0.002f);

    // The root replicates; the children do not, because each end chose its own
    // slots for them. Moving the root must still move everything.
    serverWorld.get<Transform>(spawned).position = {40.0f, 1.0f, -2.0f};
    check("and moving it afterwards reaches the client too",
          pumpUntil(frame, [&]() {
              return std::abs(clientWorld.get<Transform>(mirror).position.x - 40.0f) < 0.002f;
          }, 80));

    // Despawning takes the subtree with it.
    const size_t before = clientWorld.entityCount();
    server.despawn(serverWorld, spawned);
    check("the server drops it", !serverWorld.isAliveAtIndex(spawned.slot()));
    check("and so does the client",
          pumpUntil(frame, [&]() { return !clientWorld.isAliveAtIndex(spawned.slot()); }, 80));
    check("taking its children with it, not leaking them",
          clientWorld.entityCount() == before - 3);

    std::remove(prefabPath.c_str());
    client.close();
    server.close();
}

} // namespace

void runNetReplicationTests() {
    testTheFirstSnapshotIsTheWholeWorld();
    testALostSnapshotIsSaidAgain();
    testAnAcknowledgementOutOfOrderDoesNotUndoANewerOne();
    testABurstIsDeferredRatherThanDropped();
    testAnEntityThatLeavesTheWorldLeavesEveryClient();
    testOwnerOnlyStateGoesToItsOwnerAlone();
    testASnapshotNeverOutgrowsThePacketItRidesIn();
    testABoneTheAnimationPlacesIsNotWorthAPacket();
    testASpawnRefusesWhatItCannotMean();
    testSomethingBuiltAfterTheSceneLoadedReachesEveryone();
}
