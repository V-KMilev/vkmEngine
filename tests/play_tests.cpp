#include "support.h"

#include "framework/play_snapshot.h"

namespace {

// Play captures the world, the session runs and changes it, Stop puts it back.
// That round trip is the editor's most destructive operation - it throws away
// whatever the running game did - and nothing tested it, because the four values
// it turns on were fields on a class that needs a window to construct.

void testPlayPutsTheWorldBack() {
    std::printf("What Stop restores:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "play:cube");

    const EntityId prop = scene.createEntity();
    Transform placed;
    placed.position = {1.0f, 2.0f, 3.0f};
    scene.add<Transform>(prop, std::move(placed));
    scene.add(prop, makeName("Prop"));

    PlaySnapshot snapshot;
    check("nothing is held before Play", !snapshot.held());
    check("Play captures the world",
          snapshot.capture(scene, resources, /*dirty=*/false, /*history=*/7));
    check("  and something is held now", snapshot.held());

    // The session runs: gameplay moves things, spawns things, destroys things.
    scene.get<Transform>(prop).position = {99.0f, 99.0f, 99.0f};
    const EntityId spawned = scene.createEntity();
    scene.add<Transform>(spawned, Transform{});
    scene.add(spawned, makeName("Spawned"));

    check("Stop restores it", snapshot.restoreInto(scene, resources));

    // The world is the one Play found, not the one the session left.
    EntityId restored{};
    scene.forEach<Name>([&](EntityId id, Name& n) {
        if (std::string(n.value) == "Prop") restored = id;
    });
    check("  the entity that was there is there", bool(restored));
    check("  at the position it was placed at",
          sameDirection(scene.get<Transform>(restored).position, {1.0f, 2.0f, 3.0f}));

    size_t spawnedSurvivors = 0;
    scene.forEach<Name>([&](EntityId, Name& n) {
        if (std::string(n.value) == "Spawned") ++spawnedSurvivors;
    });
    check("  and what the session spawned is gone", spawnedSurvivors == 0);
}

void testWhatTheSnapshotRemembersAboutTheSession() {
    std::printf("What Stop reads off the snapshot:\n");

    Scene scene;
    ResourceManager resources;
    scene.add<Transform>(scene.createEntity(), Transform{});

    PlaySnapshot snapshot;
    check("captured with the flag as it stood",
          snapshot.capture(scene, resources, /*dirty=*/true, /*history=*/42));
    check("  and it remembers the flag", snapshot.dirtyAtCapture());

    // The revision is how a session that authored something is told from one
    // that only simulated: same number means the history never moved, so the
    // scene those steps addressed is the scene coming back.
    check("the same revision means nothing was authored", !snapshot.historyMoved(42));
    check("a different one means something was",           snapshot.historyMoved(43));

    snapshot.release();
    check("release ends the session", !snapshot.held());
    check("  and a released snapshot restores nothing",
          !snapshot.restoreInto(scene, resources));
}

} // namespace

void runPlayTests() {
    testPlayPutsTheWorldBack();
    testWhatTheSnapshotRemembersAboutTheSession();
}
