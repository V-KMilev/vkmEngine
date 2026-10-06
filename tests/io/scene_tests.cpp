#include "support.h"

#include <limits>

#include "io/json_file.h"

#include "ecs/component/core/missing_assets.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/environment.h"
#include "system/physics/authoring/mesh_collider.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/render/mesh.h"
#include "ecs/entity_mapping.h"
#include "ecs/hierarchy_operations.h"
#include "io/project.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/generate/mesh_generators.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "system/script/script_component.h"

namespace {

// JSON has no NaN or infinity, so a document carrying one dumps to a file nothing
// can read back. writeJsonFile writes each as zero.
void testAFileTheEngineWritesIsOneItCanReadBack() {
    std::printf("What a document carrying a non-finite number is written as:\n");

    const std::filesystem::path path =
        runScratch() / "vkm_finite" / "doc.json";
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);

    nlohmann::json doc;
    doc["ok"]       = 1.5;
    doc["nan"]      = std::numeric_limits<double>::quiet_NaN();
    doc["inf"]      = std::numeric_limits<double>::infinity();
    doc["nested"]["deep"][0] = -std::numeric_limits<double>::infinity();
    doc["untouched"] = 7;

    check("it writes", detail::writeJsonFile(path, doc, "Finite test"));

    // Read back with an unrelated parser: a file the engine cannot re-open is the
    // failure, not the number.
    nlohmann::json back;
    {
        std::ifstream in(path);
        check("  and the file parses at all", static_cast<bool>(in));
        in >> back;
    }

    check("a not-a-number is written as zero",  back["nan"].get<double>() == 0.0);
    check("  and an infinity is too",           back["inf"].get<double>() == 0.0);
    check("  however deep it sits",             back["nested"]["deep"][0].get<double>() == 0.0);
    check("and a finite number is left alone",  nearly(back["ok"].get<double>(), 1.5));
    check("  as is an integer",                 back["untouched"].get<int>() == 7);

    std::filesystem::remove_all(path.parent_path(), ec);
}

// A scene is written to its file and to the play snapshot Stop restores; a
// non-finite number would make either unreadable. Both are held to the rule.
void testASceneWithANonFiniteNumberStillRoundTrips() {
    std::printf("A scene holding a non-finite number, written both ways:\n");

    Scene scene;
    ResourceManager resources;
    const EntityId entity = scene.createEntity();
    scene.add<Transform>(entity, Transform{});
    scene.get<Transform>(entity).position = {std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f};

    const nlohmann::json snapshot = nlohmann::json::parse(
        SceneSerializer::saveToString(scene, resources),
        nullptr,
        false
    );
    const std::string position = snapshot.is_object() && !snapshot["entities"].empty()
        ? snapshot["entities"][0]["components"]["Transform"]["position"].dump()
        : std::string{};
    check("the play snapshot writes it as zero", position == "[0.0,2.0,3.0]");

    Scene restored;
    check(
        "  and reads back",
        SceneSerializer::loadFromString(SceneSerializer::saveToString(scene, resources), restored, resources)
    );

    const std::filesystem::path path = runScratch() / "vkm_finite_scene.json";
    check("the file writes", SceneSerializer::save(scene, resources, path.string()));
    Scene loaded;
    ResourceManager loadedResources;
    check("  and loads", SceneSerializer::load(loaded, loadedResources, path.string()));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// The shape a model import leaves: physics on the root, the rig on a node beneath.
// What is authored on the root must reach down; what the rig node drives must reach up.
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

    check(
        "the rig is found from the root",
        HierarchyOperations::findInSelfOrDescendants<Animator>(scene, root) == rigNode
    );
    check(
        "the mesh is found from the root, two deep",
        HierarchyOperations::findInSelfOrDescendants<Mesh>(scene, root) == meshNode
    );
    check(
        "an entity carrying it is its own answer",
        HierarchyOperations::findInSelfOrDescendants<Animator>(scene, rigNode) == rigNode
    );

    // The other way: a ragdoll on the root, reached from the rig node animation walks.
    scene.add<Ragdoll>(root, Ragdoll{});
    check(
        "a ragdoll on the root is found from the rig node",
        HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, rigNode) == root
    );
    check(
        "  and from deeper still",
        HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, meshNode) == root
    );
    check(
        "something absent is absent in both directions",
        !HierarchyOperations::findInSelfOrAncestors<Collider>(scene, meshNode)
            && !HierarchyOperations::findInSelfOrDescendants<Collider>(scene, root)
    );

    // The build places bones in the rig node's frame, not the selected entity's.
    Scene offset;
    const EntityId body = offset.createEntity();
    Transform bodyAt;
    bodyAt.position = {10.0f, 0.0f, 0.0f};
    offset.add<Transform>(body, std::move(bodyAt));

    const EntityId rigAt = offset.createEntity();
    Transform rigOffset;
    rigOffset.position = {0.0f, 2.0f, 0.0f};   // two metres above the root
    offset.add<Transform>(rigAt, std::move(rigOffset));
    offset.add<Animator>(rigAt, Animator{});
    HierarchyOperations::setParent(offset, rigAt, body);

    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t built = buildRagdoll(offset, body, skeleton);
    check("a ragdoll builds from the root", built == 4);
    check("  with the component where it was asked for", offset.has<Ragdoll>(body));

    // The hips sit a metre up the rig: (10, 3) in the rig node's frame, (10, 1) in the
    // root's. Asked in world, since a bone's Transform is relative to its group node.
    const EntityId hips = offset.get<Ragdoll>(body).bones[0].body;
    const glm::vec3 at =
        glm::vec3(HierarchyOperations::computeWorldMatrix(offset, hips)[3]);
    check("  and its bones in the rig node's frame, not the root's", at.x > 9.0f && at.y > 2.5f);
    // Grouped under one node, itself under the character, so the rig travels with it.
    const EntityId group = offset.get<Ragdoll>(body).root;
    check(
        "  grouped under a node of their own",
        group && offset.has<Hierarchy>(hips) && offset.get<Hierarchy>(hips).parent == group
    );
    check(
        "  which is itself under the character",
        offset.has<Hierarchy>(group) && offset.get<Hierarchy>(group).parent == body
    );
    check(
        "  and the bones are siblings, not nested inside each other",
        offset.get<Hierarchy>(offset.get<Ragdoll>(body).bones[1].body).parent == group
    );
}

// Fields round-tripping is not the component still working: ragdolls and joints
// reference another entity, which is the part that goes wrong.
void testComponentRoundTrip() {
    std::printf("Round-tripping components that name other entities and meshes:\n");

    Ragdoll before;
    before.active = true;
    const glm::mat4 offset =
        glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    before.bones.push_back({3, EntityId{StorageIndex{7, 2}}, offset});
    before.bones.push_back({5, EntityId{StorageIndex{9, 1}}, glm::mat4(1.0f)});

    // Names entities by slot, as a scene file does, so this stands for a save and load.
    auto bySlot   = [](EntityId e) { return e.slot(); };
    auto toSlotId = [](uint32_t slot) { return EntityId{StorageIndex{slot, 0}}; };
    const EntityNamer    name(bySlot);
    const EntityResolver resolve(toSlotId);

    Ragdoll after;
    ComponentSerializer::load(ComponentSerializer::save(before, name), after, resolve);

    check("a ragdoll keeps its switch", after.active);
    // The bodies are saved anyway; losing the mapping saves a ragdoll that has forgotten
    // them, beside a loose skeleton that falls.
    check("  and the bones that map bodies to it", after.bones.size() == 2);
    if (after.bones.size() == 2) {
        check("    naming the same bones", after.bones[0].bone == 3 && after.bones[1].bone == 5);
        check(
            "    and the same body slots",
            after.bones[0].body.slot() == 7 && after.bones[1].body.slot() == 9
        );
        // Without it the body sits at the bone, and every limb is offset by half its
        // length once physics takes over.
        check("    keeping the offset from body to bone", nearly(after.bones[0].bodyFromBone[3][1], 1.5f));
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
    check("  its length and give", nearly(loaded.distance, 2.5f) && nearly(loaded.stiffness, 0.4f));
    check("  and whether the pair still collides", loaded.collideConnected);

    // A mesh collider is its mesh's name and scale. Saving the triangles would keep a
    // copy colliding as the mesh was at save time.
    ResourceManager meshes;
    const MeshHandle cube = meshes.add(generateCube(), "scene:collider-cube");
    Collider collider;
    collider.parts.clear();
    check("a mesh collider is made", addMeshCollider(collider, cube, meshes, {2.0f, 1.0f, 2.0f}) > 0);

    const nlohmann::json saved = ComponentSerializer::save(collider, meshes);
    check(
        "a mesh collider is saved as its mesh's name",
        saved.dump().find("scene:collider-cube") != std::string::npos
    );
    check(
        "  and not as the triangles it was built into",
        saved.dump().find("meshPoints") == std::string::npos
    );

    Collider back;
    ComponentSerializer::load(saved, back, meshes);
    const bool same = back.parts.size() == 1 && back.parts[0].shape == ColliderShape::Mesh
        && back.parts[0].mesh == cube
        && back.parts[0].meshScale == glm::vec3(2.0f, 1.0f, 2.0f);
    check("it loads back naming the same mesh at the same scale", same);
    check(
        "  and built into the same triangles, with nothing to rebuild",
        back.meshPoints == collider.meshPoints && !back.meshNodes.empty()
    );
}

// Joint, Ragdoll and Hierarchy references save as slots and load back. A component
// round trip shows only that a slot number survives, not that it still names the
// same entity - that takes a whole scene.
void testSceneRoundTripKeepsReferences() {
    std::printf("A whole scene, written down and read back:\n");

    Scene scene;
    ResourceManager resources;

    // A jointed pair and a built ragdoll.
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
    check("a scene with a joint and a ragdoll in it", buildRagdoll(scene, rig, makeTestRig()) == 4);

    const size_t boneCount = scene.get<Ragdoll>(rig).bones.size();
    const std::string beamName = scene.get<Name>(beam).value;

    const std::string document = SceneSerializer::saveToString(scene, resources);
    check("  saves to a document", !document.empty());

    Scene back;
    ResourceManager backResources;
    check("  and loads back", SceneSerializer::loadFromString(document, back, backResources));

    // Found by name: the entity ids are the loader's to choose.
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

    // What a component round-trip cannot see: the reference names a live entity, the
    // right one.
    const EntityId connected = back.get<Joint>(loadedWeight).connected;
    check("  and the joint still names something that exists", connected && back.isAlive(connected));
    check(
        "    which is the entity it named before",
        back.has<Name>(connected) && beamName == back.get<Name>(connected).value
    );

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

// No gameplay module is loaded, so BehaviorRegistry is empty and every behavior type
// is unknown - the editor's state after a failed build.
void testUnknownBehaviorsSurviveASave() {
    std::printf("A behavior whose module is missing:\n");

    const nlohmann::json behaviors = nlohmann::json::array({
        {{"type", "LabWalker"}, {"properties", {{"speed", 6.5f}, {"jumpHeight", 1.25f}}}},
        {{"type", "SpinMe"}, {"properties", {{"rpm", 30.0f}}}},
    });
    const nlohmann::json authored = {{"behaviors", behaviors}};

    ScriptComponent sc;
    ComponentSerializer::load(authored, sc);

    check("cannot be constructed", sc.behaviors.empty());
    check("  but is kept, not dropped", sc.unknown.size() == 2);

    // The property text is opaque on purpose: only the unloaded module knows "speed".
    const nlohmann::json again = ComponentSerializer::save(sc);
    check("  and saving writes it back unchanged", again == authored);

    // List order is run order, so a held entry remembers its position. Appending
    // round-trips while all are unknown, and reorders once one is registered.
    check(
        "  remembering where each one sat",
        sc.unknown.size() == 2 && sc.unknown[0].index == 0 && sc.unknown[1].index == 1
    );

    // The save must also read the position back. A wholly held list round-trips in
    // order anyway, so this puts one out of order and watches it come back sorted.
    ScriptComponent shuffled;
    shuffled.unknown.push_back({"Second", "{}", 1});
    shuffled.unknown.push_back({"First",  "{}", 0});
    const nlohmann::json emitted = ComponentSerializer::save(shuffled);
    check(
        "  and the save puts each one back at its own position",
        emitted["behaviors"].size() == 2
            && emitted["behaviors"][0]["type"] == "First"
            && emitted["behaviors"][1]["type"] == "Second"
    );

    // Open, save, open again: some losses only show on the second pass.
    ScriptComponent reloaded;
    ComponentSerializer::load(again, reloaded);
    check("  through any number of saves", ComponentSerializer::save(reloaded) == authored);
}

void testAProjectSurvivesBeingWritten() {
    std::printf("What a project.json keeps when a tool writes it back:\n");

    const std::filesystem::path root =
        runScratch() / "vkm_project_write_test";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    // Hand-authored, with a key this build does not know and a splash list loadProject
    // reads but saveProject deliberately does not write.
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
    check(
        "and everything that was not edited",
        reread.name == "Written By Hand"
            && reread.entryScene == "scenes/start.json"
            && reread.tickRate == 90
            && reread.netPort == 27888
    );

    // What the writer must not lose: an unmodelled key, and a list it reads but never writes.
    nlohmann::json doc;
    {
        std::ifstream in(root / "project.json");
        in >> doc;
    }
    check("a key this build does not model is still there", doc.contains("somethingThisBuildDoesNotKnow"));
    check(
        "and the splash list it never writes",
        doc.contains("splash") && doc["splash"].is_array() && doc["splash"].size() == 1
    );

    // A project.json with no render block (as above) opens with the engine's defaults,
    // not everything switched off.
    const RenderSettings defaults;
    check(
        "a project with no render block keeps the engine's look",
        reread.render.bloom == defaults.bloom && reread.render.gtao == defaults.gtao
    );

    Project tuned = reread;
    tuned.render.bloom            = !defaults.bloom;
    tuned.render.bloomStrength    = 0.375f;
    tuned.render.textureFiltering = TextureFiltering::Nearest;
    tuned.render.tonemap          = Tonemap::ACES;
    tuned.render.exposure         = 1.5f;
    tuned.render.cullMaxDistance  = 275.0f;
    tuned.render.renderMode       = RenderMode::Normals;   // editor view state
    check("a tuned look writes", saveProject(root, tuned));

    Project shipped;
    check("and reads back", loadProject(root, shipped));
    check("carrying the toggle",  shipped.render.bloom == !defaults.bloom);
    check("and the value",        nearly(shipped.render.bloomStrength, 0.375f));
    check("and the named enum",   shipped.render.textureFiltering == TextureFiltering::Nearest);
    check("and the display transform", shipped.render.tonemap == Tonemap::ACES);
    check("and the exposure it is landed at", nearly(shipped.render.exposure, 1.5f));
    check("and the cull distance",      nearly(shipped.render.cullMaxDistance, 275.0f));

    // What must NOT travel: a debug buffer is the editor's view, not the game's look.
    check("but not the debug view", shipped.render.renderMode == defaults.renderMode);
    {
        std::ifstream in(root / "project.json");
        nlohmann::json written;
        in >> written;
        check(
            "which is not even written",
            written.contains("render") && !written["render"].contains("renderMode")
        );

        // Every enum is a name on disk, not an ordinal, or enumerator order becomes part
        // of the format. Asked of the document, which a round trip cannot see.
        const nlohmann::json& block = written["render"];
        check(
            "and the enums are written by name",
            block["tonemap"].is_string() && block["textureFiltering"].is_string()
        );
    }

    std::filesystem::remove_all(root, ec);
}

// A mistyped render setting keeps its default and warns; throwing would lose every
// later setting, and the splash list.
void testAMistypedRenderSettingCostsOneSetting() {
    std::printf("A project.json with one render setting of the wrong type:\n");

    const ScratchProject project("vkm_project_mistyped");
    {
        std::ofstream out(project.root() / "project.json");
        out << R"({
    "name": "Mistyped",
    "render": {"bloom": "yes", "gtaoRadius": 2.5},
    "splash": [{"image": "logo.png", "seconds": 2.0}]
})";
    }

    Project loaded;
    check("the project still reads", loadProject(project.root(), loaded));
    check("  the mistyped setting keeps its default", loaded.render.bloom == RenderSettings{}.bloom);
    check("  the settings after it read", nearly(loaded.render.gtaoRadius, 2.5f));
    check("  and so does the rest of the file", loaded.splash.size() == 1);
}

// Values the renderer divides by, read from a typed file. Zero GTAO radius and phase
// asymmetry of one are each 0/0 on the GPU - NaN that bloom spreads across the screen;
// an MSAA count the scene target lacks is passed to the driver as is.
void testAuthoredValuesTheRendererDividesByAreBounded() {
    std::printf("A project and a scene naming values the renderer cannot draw:\n");

    const ScratchProject project("vkm_project_unbounded");
    {
        std::ofstream out(project.root() / "project.json");
        out << R"({"name": "Unbounded", "render": {"gtaoRadius": 0.0, "msaaSamples": 3}})";
    }
    Project loaded;
    check("the project reads", loadProject(project.root(), loaded));
    check(
        "  with its GTAO radius held above zero",
        nearly(loaded.render.gtaoRadius, RenderSettings::MIN_GTAO_RADIUS)
    );
    check(
        "  and a sample count the target is built with",
        loaded.render.msaaSamples == RenderSettings{}.msaaSamples
    );

    const nlohmann::json environment = {
        {"sky", {{"mieG", 1.0}}},
        {"fog", {{"anisotropy", -1.0}}},
    };
    Environment env;
    ComponentSerializer::load(environment, env);
    check("a sky's phase asymmetry is held below one", nearly(env.sky.mieG, SkySettings::MAX_MIE_G));
    check("  and the fog's above minus one", nearly(env.fog.anisotropy, -FogSettings::MAX_ANISOTROPY));
}

// What the editor's empty state rests on: a directory with no project.json above it
// is not a project, `ProjectController::open` refuses it, and the editor shows a
// picker instead of a workspace.
void testWhatIsAndIsNotAProject() {
    std::printf("What counts as a project:\n");

    const std::filesystem::path root =
        runScratch() / "vkm_project_predicate";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "scenes", ec);

    check("a bare directory is not one", findProjectRoot(root).empty());
    check("  nor is anything inside it", findProjectRoot(root / "scenes").empty());

    {
        std::ofstream out(root / "project.json");
        out << R"({"name":"Probe"})";
    }

    check("a directory holding project.json is one", !findProjectRoot(root).empty());
    // Paths inside count, so an author can drop a scene file on the editor and have it
    // find its project.
    check("  and so is anything inside it", !findProjectRoot(root / "scenes").empty());
    check(
        "  which resolves to the directory itself, not the child",
        findProjectRoot(root / "scenes") == findProjectRoot(root)
    );

    std::filesystem::remove_all(root, ec);
}

void testTheSceneOfRecordStillOpens() {
    std::printf("The scene the engine keeps as its record of the format:\n");

    // engine.md names obstacle_course.json the scene of record that a format change
    // must round-trip; this does it here rather than in front of an author.
    const std::filesystem::path scene =
        std::filesystem::path(VKM_EXAMPLES_DIR) / "physics_lab" / "scenes" / "obstacle_course.json";

    std::error_code ec;
    if (!std::filesystem::exists(scene, ec)) {
        // A packaged SDK ships no examples/.
        std::printf("      (no examples/ in this build - skipped)\n");
        return;
    }

    // No asset factories here, so every named mesh and material is unresolved and says
    // so; unresolved references are kept, so these errors are the format working.
    std::printf("      (the unresolved-asset errors below are expected: no factories here)\n");

    Scene world;
    ResourceManager resources;
    check("the scene of record loads", SceneSerializer::load(world, resources, scene.string()));

    // Four characters and the props they walk over - so a load that produced an empty
    // world does not pass.
    size_t characters = 0;
    world.forEach<CharacterController, Transform>([&](EntityId, CharacterController&, Transform&) {
        ++characters;
    });
    check("carrying the four characters it is authored with", characters == 4);

    // Every character starts on something: one authored in the air falls on load,
    // playing its jump clip - reading as an animation bug, not a scene error.
    bool allResting = true;
    world.forEach<CharacterController, Collider>([&](EntityId id, CharacterController&, Collider& collider) {
        const Transform& at = world.get<Transform>(id);
        for (const ColliderPart& part : collider.parts) {
            if (part.shape != ColliderShape::Capsule) continue;
            const float bottom = at.position.y + part.center.y - part.halfHeight - part.radius;
            if (bottom > 0.0f) allResting = false;
        }
    });
    check("each of them resting on the floor rather than above it", allResting);

    // The format must also write the file back and read its own output - to the temp
    // directory, never over a shipped asset.
    const std::filesystem::path echoed =
        runScratch() / "vkm_scene_of_record.json";
    check("  and saves back out", SceneSerializer::save(world, resources, echoed.string()));

    Scene reread;
    ResourceManager rereadResources;
    check("  and what it wrote loads again", SceneSerializer::load(reread, rereadResources, echoed.string()));

    size_t rereadCharacters = 0;
    reread.forEach<CharacterController, Transform>([&](EntityId, CharacterController&, Transform&) {
        ++rereadCharacters;
    });
    check("  with the same four characters in it", rereadCharacters == characters);
    check("  and the same entity count", reread.entityCount() == world.entityCount());

    std::filesystem::remove(echoed, ec);
}

// A component naming assets goes through the same reflection driver, which carries
// Handle<T>: the handle must arrive as the same asset, with the fields beside it intact.
void testAssetHandlesSurviveARoundTrip() {
    std::printf("A component that names an asset, written down and read back:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "test:cube");

    const EntityId prop = scene.createEntity();
    scene.add<Transform>(prop, Transform{});
    Mesh mesh;
    mesh.mesh        = resources.findByName<MeshAsset>("test:cube");
    mesh.visible     = true;
    mesh.castShadows = false;          // the non-default, so a lost field shows
    scene.add<Mesh>(prop, std::move(mesh));

    Animator animator;
    animator.speed       = 2.5f;
    animator.playOnStart = false;
    animator.looping     = false;
    animator.playing     = true;       // transient: must NOT survive
    scene.add<Animator>(prop, std::move(animator));

    check("the mesh names an asset", bool(scene.get<Mesh>(prop).mesh));

    const std::string document = SceneSerializer::saveToString(scene, resources);
    check("  the scene saves", !document.empty());

    // The name is the identity, so the far end resolves it against its own manager.
    Scene back;
    ResourceManager backResources;
    backResources.add(generateCube(), "test:cube");
    check("  and loads back", SceneSerializer::loadFromString(document, back, backResources));

    EntityId landed{};
    back.forEach<Mesh>([&](EntityId id, Mesh&) { landed = id; });
    check("  with the mesh entity", bool(landed));

    const Mesh& read = back.get<Mesh>(landed);
    check("  the handle resolved against the far manager", bool(read.mesh));
    check("  to the asset of that name", read.mesh == backResources.findByName<MeshAsset>("test:cube"));
    check("  and the flag beside it survived", read.castShadows == false);

    const Animator& anim = back.get<Animator>(landed);
    check(
        "  the animator's authored fields survived",
        nearly(anim.speed, 2.5f) && !anim.playOnStart && !anim.looping
    );
    // Reflection marks "transient" by not listing the field; this catches the reflect
    // block quietly gaining one.
    check("  and its transient playback state did not", anim.playing == false);

    // A vector of reflected structs, as Collider and LOD are written. Two parts, so an
    // off-by-one in the walk shows as a count.
    Scene compound;
    ResourceManager none;
    const EntityId body = compound.createEntity();
    compound.add<Transform>(body, Transform{});
    Collider collider;
    collider.isTrigger = true;
    collider.parts.clear();
    ColliderPart box;
    box.shape       = ColliderShape::Box;
    box.halfExtents = {2.0f, 3.0f, 4.0f};
    ColliderPart capsule;
    capsule.shape      = ColliderShape::Capsule;
    capsule.radius     = 0.25f;
    capsule.halfHeight = 1.5f;
    capsule.center     = {0.0f, 5.0f, 0.0f};
    collider.parts = {box, capsule};
    compound.add<Collider>(body, std::move(collider));

    const std::string doc = SceneSerializer::saveToString(compound, none);
    Scene readBack;
    ResourceManager readResources;
    check(
        "a collider of two parts saves and loads",
        SceneSerializer::loadFromString(doc, readBack, readResources)
    );

    EntityId got{};
    readBack.forEach<Collider>([&](EntityId e, Collider&) { got = e; });
    check("  the entity is there", bool(got));
    const Collider& shape = readBack.get<Collider>(got);
    check("  with both parts",     shape.parts.size() == 2);
    check(
        "  the box first",
        shape.parts.size() == 2
            && shape.parts[0].shape == ColliderShape::Box
            && sameDirection(shape.parts[0].halfExtents, {2.0f, 3.0f, 4.0f})
    );
    check(
        "  the capsule second",
        shape.parts.size() == 2
            && shape.parts[1].shape == ColliderShape::Capsule
            && nearly(shape.parts[1].radius, 0.25f)
            && nearly(shape.parts[1].halfHeight, 1.5f)
            && sameDirection(shape.parts[1].center, {0.0f, 5.0f, 0.0f})
    );
    check("  and the flag beside them", shape.isTrigger);
}

// An unresolved reference is kept so a later save writes the name back rather than
// "" - but only where the save has a key for it. A LOD level is an array element and
// the save writes the whole ramp from live handles, so keeping it would leave the
// assets block naming an asset the scene does not.
void testAnUnresolvedArrayElementIsNotKept() {
    std::printf("A missing asset named from inside an array:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "keep:cube");

    const EntityId prop = scene.createEntity();
    scene.add<Transform>(prop, Transform{});
    Mesh mesh;
    mesh.mesh = resources.findByName<MeshAsset>("keep:cube");
    scene.add<Mesh>(prop, std::move(mesh));

    // A named field and an array element, both naming a mesh nothing holds.
    const nlohmann::json level  = {{"mesh", "gone:in_array"}, {"maxDistance", 30.0f}};
    const nlohmann::json levels = nlohmann::json::array({level});
    const nlohmann::json components = {
        {"Mesh", {{"mesh", "gone:named"}, {"material", ""}, {"visible", true}, {"castShadows", true}}},
        {"LOD", {{"bias", 1.0f}, {"levels", levels}}},
    };

    std::printf("      (the unresolved-asset errors below are expected)\n");
    Scene into;
    ResourceManager empty;
    const EntityId loaded = into.createEntity();
    auto toSlotId = [&](uint32_t slot) { return into.entityAt(slot); };
    const EntityResolver resolve(toSlotId);
    SceneSerializer::loadComponents(components, into, loaded, empty, resolve);

    check("the entity kept a record of what did not resolve", into.has<MissingAssets>(loaded));
    if (!into.has<MissingAssets>(loaded)) return;

    const std::vector<MissingAssetRef>& refs = into.get<MissingAssets>(loaded).refs;
    bool named = false;
    bool fromArray = false;
    for (const MissingAssetRef& ref : refs) {
        if (ref.name == "gone:named")    named = true;
        if (ref.name == "gone:in_array") fromArray = true;
    }
    check("  the one in a named field is kept, so a save can put it back", named);
    check("  and the one from inside an array is not", !fromArray);
}

// The record of what did not load is never trimmed. Undoing a replacement pick
// empties the field again and the save must restore the name, so readers ask whether
// each field is still empty rather than the record forgetting filled ones.
void testAKeptNameSurvivesAFillThatIsTakenBack() {
    std::printf("A missing asset filled, then emptied again:\n");

    ResourceManager resources;
    resources.add(generateCube(), "keep:cube");
    const MeshHandle cube = resources.findByName<MeshAsset>("keep:cube");

    const nlohmann::json components = {
        {"Mesh", {{"mesh", "gone:mesh"}, {"material", ""}, {"visible", true}, {"castShadows", true}}},
    };

    std::printf("      (the unresolved-asset error below is expected)\n");
    Scene scene;
    const EntityId prop = scene.createEntity();
    auto toSlotId = [&](uint32_t slot) { return scene.entityAt(slot); };
    const EntityResolver resolve(toSlotId);
    SceneSerializer::loadComponents(components, scene, prop, resources, resolve);
    check(
        "the name that did not resolve is reported",
        SceneSerializer::unresolvedRefs(scene, resources, prop).size() == 1
    );

    scene.get<Mesh>(prop).mesh = cube;
    check("  a field filled since is not", SceneSerializer::unresolvedRefs(scene, resources, prop).empty());

    scene.get<Mesh>(prop).mesh = MeshHandle{};
    check(
        "  and emptied again, it is once more",
        SceneSerializer::unresolvedRefs(scene, resources, prop).size() == 1
    );

    nlohmann::json saved = nlohmann::json::object();
    SceneSerializer::saveComponents(scene, prop, saved, resources);
    check("  so the save still writes the name back", saved["Mesh"]["mesh"] == "gone:mesh");
}

// A scene's render tuning: a camera's widest blur, a light's normal bias, a volume's
// blend, the fog's reach. A reader forgetting one would load its default silently.
void testTheRenderTuningSurvivesASave() {
    std::printf("A scene's render tuning, saved and loaded:\n");

    Camera lens;
    lens.dofMaxBlur = 0.027f;
    Camera lensBack;
    ComponentSerializer::load(ComponentSerializer::save(lens), lensBack);
    check("a camera keeps its widest blur", nearly(lensBack.dofMaxBlur, 0.027f));

    Light sun;
    sun.shadowNormalBias = 2.75f;
    Light sunBack;
    ComponentSerializer::load(ComponentSerializer::save(sun), sunBack);
    check("a light keeps its normal bias", nearly(sunBack.shadowNormalBias, 2.75f));

    IrradianceVolume volume;
    volume.blendDistance = 3.5f;
    IrradianceVolume volumeBack;
    ComponentSerializer::load(ComponentSerializer::save(volume), volumeBack);
    check("an irradiance volume keeps its blend distance", nearly(volumeBack.blendDistance, 3.5f));

    Scene scene;
    ResourceManager resources;
    scene.environment().fog.maxDistance = 650.0f;
    const std::string document = SceneSerializer::saveToString(scene, resources);
    Scene back;
    ResourceManager backResources;
    check(
        "a scene with fog in it saves and loads",
        SceneSerializer::loadFromString(document, back, backResources)
    );
    check("  keeping how far the fog reaches", nearly(back.environment().fog.maxDistance, 650.0f));
}

} // namespace

void runSceneTests() {
    testAFileTheEngineWritesIsOneItCanReadBack();
    testASceneWithANonFiniteNumberStillRoundTrips();
    testImportedHierarchy();
    testComponentRoundTrip();
    testSceneRoundTripKeepsReferences();
    testUnknownBehaviorsSurviveASave();
    testAProjectSurvivesBeingWritten();
    testAMistypedRenderSettingCostsOneSetting();
    testAuthoredValuesTheRendererDividesByAreBounded();
    testWhatIsAndIsNotAProject();
    testTheSceneOfRecordStillOpens();
    testAssetHandlesSurviveARoundTrip();
    testAnUnresolvedArrayElementIsNotKept();
    testAKeptNameSurvivesAFillThatIsTakenBack();
    testTheRenderTuningSurvivesASave();
}
