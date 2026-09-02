#include "support.h"

namespace {

// The shape a model import actually leaves behind: physics on the root, the rig
// on a node beneath it. Everything authored on the root has to reach downward,
// and everything driven from the rig node has to reach up.
void testImportedHierarchy() {
    std::printf("Authoring across an imported hierarchy:\n");

    Scene scene;
    const EntityId root = scene.createEntity();
    scene.add<Transform>(root, Transform{});
    scene.add<Name>(root, makeName("Character"));

    const EntityId rigNode = scene.createEntity();
    scene.add<Transform>(rigNode, Transform{});
    scene.add<Name>(rigNode, makeName("RootNode"));
    scene.add<Animator>(rigNode, Animator{});
    HierarchyOperations::setParent(scene, rigNode, root);

    const EntityId meshNode = scene.createEntity();
    scene.add<Transform>(meshNode, Transform{});
    scene.add<Mesh>(meshNode, Mesh{});
    HierarchyOperations::setParent(scene, meshNode, rigNode);

    check("the rig is found from the root",
          HierarchyOperations::findInSelfOrDescendants<Animator>(scene, root)
              == rigNode);
    check("the mesh is found from the root, two deep",
          HierarchyOperations::findInSelfOrDescendants<Mesh>(scene, root)
              == meshNode);
    check("an entity carrying it is its own answer",
          HierarchyOperations::findInSelfOrDescendants<Animator>(scene, rigNode)
              == rigNode);

    // The other direction: a ragdoll authored on the root, reached from the rig
    // node the animation system walks.
    scene.add<Ragdoll>(root, Ragdoll{});
    check("a ragdoll on the root is found from the rig node",
          HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, rigNode)
              == root);
    check("  and from deeper still",
          HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, meshNode)
              == root);
    check("something absent is absent in both directions",
          !HierarchyOperations::findInSelfOrAncestors<Collider>(scene, meshNode)
       && !HierarchyOperations::findInSelfOrDescendants<Collider>(scene, root));

    // And the build places bones in the rig node's frame rather than the
    // selected entity's, which matters the moment the two differ.
    Scene offset;
    const EntityId body = offset.createEntity();
    Transform bodyAt;
    bodyAt.position = {10.0f, 0.0f, 0.0f};
    offset.add<Transform>(body, std::move(bodyAt));

    const EntityId rigAt = offset.createEntity();
    Transform rigOffset;
    rigOffset.position = {0.0f, 2.0f, 0.0f};   // a metre or two above the root
    offset.add<Transform>(rigAt, std::move(rigOffset));
    offset.add<Animator>(rigAt, Animator{});
    HierarchyOperations::setParent(offset, rigAt, body);

    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t built = buildRagdoll(offset, body, skeleton);
    check("a ragdoll builds from the root", built == 4);
    check("  with the component where it was asked for",
          offset.has<Ragdoll>(body));

    // The hips bone sits a metre up the rig; in the rig node's frame that is
    // 10 across and 3 up, and in the root's it would be 10 across and 1. Asked
    // in world, because a bone is parented to the character it belongs to and
    // so its own Transform is measured from there.
    const EntityId hips = offset.get<Ragdoll>(body).bones[0].body;
    const glm::vec3 at =
        glm::vec3(HierarchyOperations::computeWorldMatrix(offset, hips)[3]);
    check("  and its bones in the rig node's frame, not the root's",
          at.x > 9.0f && at.y > 2.5f);
    // Grouped under one node rather than tipped into the character's child
    // list, and that node under the character - so the rig travels with it.
    const EntityId group = offset.get<Ragdoll>(body).root;
    check("  grouped under a node of their own",
          group && offset.has<Hierarchy>(hips)
       && offset.get<Hierarchy>(hips).parent == group);
    check("  which is itself under the character",
          offset.has<Hierarchy>(group)
       && offset.get<Hierarchy>(group).parent == body);
    check("  and the bones are siblings, not nested inside each other",
          offset.get<Hierarchy>(offset.get<Ragdoll>(body).bones[1].body).parent
              == group);
}

// What survives being written down. A component whose fields round-trip is not
// the same as one that still works afterwards, and both ragdolls and joints
// carry a reference to another entity, which is the part that goes wrong.
void testComponentRoundTrip() {
    std::printf("Round-tripping the new components:\n");

    Ragdoll before;
    before.active = true;
    const glm::mat4 offset =
        glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    before.bones.push_back({3, EntityId{StorageIndex{7, 2}}, offset});
    before.bones.push_back({5, EntityId{StorageIndex{9, 1}}, glm::mat4(1.0f)});

    // A carrier naming entities by slot, which is what a scene file does, so a
    // reference that survives this round trip survives a save and load.
    auto bySlot   = [](EntityId e) { return e.slot(); };
    auto toSlotId = [](uint32_t slot) { return EntityId{StorageIndex{slot, 0}}; };
    const EntityNamer    name(bySlot);
    const EntityResolver resolve(toSlotId);

    Ragdoll after;
    ComponentSerializer::load(ComponentSerializer::save(before, name), after, resolve);

    check("a ragdoll keeps its switch", after.active);
    // The bodies are entities the scene saves anyway. Losing the mapping does
    // not save a ragdoll without its bodies - it saves one that has forgotten
    // them, beside a loose skeleton that falls.
    check("  and the bones that map bodies to it", after.bones.size() == 2);
    if (after.bones.size() == 2) {
        check("    naming the same bones", after.bones[0].bone == 3
                                        && after.bones[1].bone == 5);
        check("    and the same body slots", after.bones[0].body.slot() == 7
                                          && after.bones[1].body.slot() == 9);
        // Without this the body sits where the bone is, and every limb is
        // offset by half its own length the moment physics takes over.
        check("    keeping the offset from body to bone",
              nearly(after.bones[0].boneFromBody[3][1], 1.5f));
    }

    Joint joint;
    joint.type = JointType::Distance;
    joint.connected = EntityId{StorageIndex{4, 3}};
    joint.anchor = {0.0f, 0.5f, 0.0f};
    joint.distance = 2.5f;
    joint.stiffness = 0.4f;
    joint.collideConnected = true;

    Joint loaded;
    ComponentSerializer::load(ComponentSerializer::save(joint, name), loaded, resolve);

    check("a joint keeps its type", loaded.type == JointType::Distance);
    check("  its connected slot", loaded.connected.slot() == 4);
    check("  its anchor", nearly(loaded.anchor.y, 0.5f));
    check("  its length and give", nearly(loaded.distance, 2.5f)
                                && nearly(loaded.stiffness, 0.4f));
    check("  and whether the pair still collides", loaded.collideConnected);

    // A collider's points and its parts' spans into them, which is the whole
    // description of a mesh: lose either and the shape is empty.
    Collider collider;
    collider.parts.clear();
    // Padded so the span starts somewhere other than zero: a round-trip that
    // leaves meshFirst at its default cannot tell the field from its absence.
    collider.meshPoints = {{9,9,9}, {9,9,9}, {9,9,9}, {0,0,0}, {1,0,0}, {0,1,0}};
    ColliderPart part;
    part.shape = ColliderShape::Mesh;
    part.meshFirst = 3;
    part.meshCount = 3;
    collider.parts = { part };

    Collider back;
    ComponentSerializer::load(ComponentSerializer::save(collider), back);
    check("a mesh collider keeps its points", back.meshPoints.size() == 6);
    const bool span = back.parts.size() == 1
                   && back.parts[0].meshFirst == 3
                   && back.parts[0].meshCount == 3
                   && back.parts[0].shape == ColliderShape::Mesh;
    check("  and the span that reads them", span);
}

// A ragdoll doubles as a hit box rig: its bones are bodies with colliders, so a
// query already picks one out. This is the whole path a game walks - shoot,
// find the limb, find whose it is.
// The branch made two components carry references to other entities, and taught
// the loader to recover them from the slots a save writes. Nothing tested the
// pair together: the component round-trips checked that a slot number survives,
// which is not the same as the reference still naming the entity it named.
void testSceneRoundTripKeepsReferences() {
    std::printf("A whole scene, written down and read back:\n");

    Scene scene;
    ResourceManager resources;

    // A jointed pair and a built ragdoll, which is every cross-entity reference
    // the branch added.
    const EntityId beam = scene.createEntity();
    scene.add<Transform>(beam, Transform{});
    scene.add(beam, makeName("Beam"));

    const EntityId weight = scene.createEntity();
    Transform at;
    at.position = {0.0f, -2.0f, 0.0f};
    scene.add<Transform>(weight, std::move(at));
    scene.add(weight, makeName("Weight"));
    Rigidbody body;
    body.mass = 5.0f;
    scene.add<Rigidbody>(weight, std::move(body));
    Joint rope;
    rope.type = JointType::Distance;
    rope.connected = beam;
    rope.distance = 2.0f;
    scene.add<Joint>(weight, std::move(rope));

    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});
    scene.add(rig, makeName("Rig"));
    check("a scene with a joint and a ragdoll in it",
          buildRagdoll(scene, rig, makeTestRig()) == 4);

    const size_t boneCount = scene.get<Ragdoll>(rig).bones.size();
    const std::string beamName = scene.get<Name>(beam).value;

    const std::string document = SceneSerializer::saveToString(scene, resources);
    check("  saves to a document", !document.empty());

    Scene back;
    ResourceManager backResources;
    check("  and loads back",
          SceneSerializer::loadFromString(document, back, backResources));

    // Found by name, because the entity ids are the loader's to choose.
    EntityId loadedWeight{};
    EntityId loadedRig{};
    back.forEachEntity([&](EntityId id) {
        if (!back.has<Name>(id)) return;
        const std::string name = back.get<Name>(id).value;
        if (name == "Weight") loadedWeight = id;
        if (name == "Rig")    loadedRig = id;
    });
    check("  the jointed body came back", loadedWeight && back.has<Joint>(loadedWeight));
    check("  the rig came back", loadedRig && back.has<Ragdoll>(loadedRig));

    // The part that a component round-trip cannot see: the reference has to name
    // a live entity, and the right one.
    const EntityId connected = back.get<Joint>(loadedWeight).connected;
    check("  and the joint still names something that exists",
          connected && back.isAlive(connected));
    check("    which is the entity it named before",
          back.has<Name>(connected) && beamName == back.get<Name>(connected).value);

    const Ragdoll& loadedRagdoll = back.get<Ragdoll>(loadedRig);
    check("  the ragdoll kept all of its bones", loadedRagdoll.bones.size() == boneCount);
    bool everyBoneAlive = loadedRagdoll.root && back.isAlive(loadedRagdoll.root);
    for (const RagdollBone& bone : loadedRagdoll.bones) {
        if (!bone.body || !back.isAlive(bone.body) || !back.has<Rigidbody>(bone.body)) {
            everyBoneAlive = false;
        }
    }
    check("    and every one of them is a live body", everyBoneAlive);
}

// No gameplay module is loaded here, so BehaviorRegistry is empty and every
// type name in a scene document is unknown - the state the editor is in after a
// failed build.
void testUnknownBehaviorsSurviveASave() {
    std::printf("A behavior whose module is missing:\n");

    const nlohmann::json authored = {
        {"behaviors", nlohmann::json::array({
            {{"type", "LabWalker"},
             {"properties", {{"speed", 6.5f}, {"jumpHeight", 1.25f}}}},
            {{"type", "SpinMe"}, {"properties", {{"rpm", 30.0f}}}},
        })}
    };

    ScriptComponent sc;
    ComponentSerializer::load(authored, sc);

    check("cannot be constructed", sc.behaviors.empty());
    check("  but is kept, not dropped", sc.unknown.size() == 2);

    // The property text is opaque here on purpose: the code that knows what
    // "speed" means is the module that is not loaded.
    const nlohmann::json again = ComponentSerializer::save(sc);
    check("  and saving writes it back unchanged", again == authored);

    // The list's order is the order the behaviors run in, so a held one has to
    // remember where it sat. Appending them all at the end round-trips
    // byte-identically here - every entry is unknown - and reorders the moment
    // one type in the list is registered and the rest are not.
    check("  remembering where each one sat",
          sc.unknown.size() == 2 && sc.unknown[0].index == 0 && sc.unknown[1].index == 1);

    // Recording the position is only half of it; the save has to read it back.
    // A list that is entirely held round-trips in order either way, so the
    // assertion has to put one out of order and watch it come back sorted -
    // which is what an implementation that appends cannot do.
    ScriptComponent shuffled;
    shuffled.unknown.push_back({"Second", "{}", 1});
    shuffled.unknown.push_back({"First",  "{}", 0});
    const nlohmann::json emitted = ComponentSerializer::save(shuffled);
    check("  and the save puts each one back at its own position",
          emitted["behaviors"].size() == 2
          && emitted["behaviors"][0]["type"] == "First"
          && emitted["behaviors"][1]["type"] == "Second");

    // The editor opens, saves, and opens again, so the loss to catch is the one
    // that only shows on the second pass.
    ScriptComponent reloaded;
    ComponentSerializer::load(again, reloaded);
    check("  through any number of saves", ComponentSerializer::save(reloaded) == authored);
}

void testAProjectSurvivesBeingWritten() {
    std::printf("What a project.json keeps when a tool writes it back:\n");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "vkm_project_write_test";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    // Hand-authored, with a key this build has never heard of and a splash list
    // loadProject reads but saveProject deliberately does not write.
    {
        std::ofstream out(root / "project.json");
        out << R"({
    "name": "Written By Hand",
    "engineVersion": "0.0.1",
    "entryScene": "scenes/start.json",
    "tickRate": 90,
    "maxPlayers": 3,
    "netPort": 27888,
    "splash": [{"image": "logo.png", "seconds": 2.0}],
    "somethingThisBuildDoesNotKnow": {"kept": true}
})";
    }

    Project project;
    check("a hand-authored project reads", loadProject(root, project));
    check("with the seats it names", project.maxPlayers == 3);
    check("and the port", project.netPort == 27888);

    project.maxPlayers = 8;
    check("and writes back", saveProject(root, project));

    Project reread;
    check("what was written reads again", loadProject(root, reread));
    check("carrying the edit", reread.maxPlayers == 8);
    check("and everything that was not edited", reread.name == "Written By Hand"
                                             && reread.entryScene == "scenes/start.json"
                                             && reread.tickRate == 90
                                             && reread.netPort == 27888);

    // The two the writer must not lose: a key it does not model, and a list it
    // reads but never writes. A writer that rebuilt the document would drop
    // both, and the author would find out a week later.
    nlohmann::json doc;
    {
        std::ifstream in(root / "project.json");
        in >> doc;
    }
    check("a key this build does not model is still there",
          doc.contains("somethingThisBuildDoesNotKnow"));
    check("and the splash list it never writes",
          doc.contains("splash") && doc["splash"].is_array() && doc["splash"].size() == 1);

    std::filesystem::remove_all(root, ec);
}

void testTheSceneOfRecordStillOpens() {
    std::printf("The scene the engine keeps as its record of the format:\n");

    // engine.md calls obstacle_course.json the saved scene of record and asks
    // that a format change be round-tripped against it. Nothing does that
    // automatically, so a change that broke it would surface in the editor.
    const std::filesystem::path scene =
        std::filesystem::path(VKM_EXAMPLES_DIR) / "physics_lab" / "scenes" / "obstacle_course.json";

    std::error_code ec;
    if (!std::filesystem::exists(scene, ec)) {
        // A packaged SDK ships no examples/. Saying so beats failing on a file
        // that was never meant to be there.
        std::printf("      (no examples/ in this build - skipped)\n");
        return;
    }

    // This binary wires no asset factories, so every mesh and material the file
    // names resolves to nothing and says so. That is the format behaving as
    // designed - a reference that did not resolve is kept, not erased - and the
    // errors below belong to the load, not to a failure.
    std::printf("      (the unresolved-asset errors below are expected: no factories here)\n");

    Scene world;
    ResourceManager resources;
    check("the scene of record loads",
          SceneSerializer::load(world, resources, scene.string()));

    // What the lab is: four characters and the props they walk over. A load
    // that returned true having produced an empty world would pass the line
    // above and nothing else.
    size_t characters = 0;
    world.forEach<CharacterController, Transform>([&](EntityId, CharacterController&, Transform&) {
        ++characters;
    });
    check("carrying the four characters it is authored with", characters == 4);

    // Every character starts on something. A capsule authored in the air is a
    // character that falls on load, plays its jump clip while it does, and
    // reads as an animation bug rather than as the scene being wrong - which is
    // exactly how this was found.
    bool allResting = true;
    world.forEach<CharacterController, Collider>(
            [&](EntityId id, CharacterController&, Collider& collider) {
        const Transform& at = world.get<Transform>(id);
        for (const ColliderPart& part : collider.parts) {
            if (part.shape != ColliderShape::Capsule) continue;
            const float bottom = at.position.y + part.center.y - part.halfHeight - part.radius;
            if (bottom > 0.0f) allResting = false;
        }
    });
    check("each of them resting on the floor rather than above it", allResting);
}

} // namespace

void runSceneTests() {
    testImportedHierarchy();
    testComponentRoundTrip();
    testSceneRoundTripKeepsReferences();
    testUnknownBehaviorsSurviveASave();
    testAProjectSurvivesBeingWritten();
    testTheSceneOfRecordStillOpens();
}
