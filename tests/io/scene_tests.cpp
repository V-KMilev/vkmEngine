#include "support.h"

#include <algorithm>
#include <clocale>
#include <fstream>
#include <limits>
#include <string>

#include "io/asset/asset_serializer.h"
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
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/terrain.h"
#include "ecs/entity_mapping.h"
#include "ecs/hierarchy_operations.h"
#include "io/project.h"
#include "io/scene/component_serializer.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_serializer.h"
#include "resource/generate/mesh_generators.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "system/script/reflected_behavior.h"
#include "system/script/script_component.h"
#include "io/reflect_json.h"

// A project's own settings file, as ReflectJson writes and reads it.
namespace ReflectJsonTest {
enum class Quality : uint8_t { Low, High, Count };
struct Key {
    std::string action;
    int         code = 0;
};
struct Settings {
    float            volume  = 0.8f;
    bool             vsync   = true;
    std::string      name    = "Player";
    Quality          quality = Quality::High;
    glm::vec3        tint    = {1.0f, 1.0f, 1.0f};
    std::vector<Key> keys;
};
} // namespace ReflectJsonTest

VKM_ENUM_NAMES(::ReflectJsonTest::Quality, "Low", "High")
VKM_REFLECT_BEGIN(::ReflectJsonTest::Key)
    VKM_F(action)
    VKM_F(code)
VKM_REFLECT_END()
VKM_REFLECT_BEGIN(::ReflectJsonTest::Settings)
    VKM_F(volume)
    VKM_F(vsync)
    VKM_F(name)
    VKM_F(quality)
    VKM_F(tint)
    VKM_F(keys)
VKM_REFLECT_END()

// A behavior whose module is loaded, beside held ones whose module is not.
namespace HeldOrderTest {
class Built : public ReflectedBehavior<Built> {
    public:
        int step = 0;
};
} // namespace HeldOrderTest

VKM_REFLECT_BEGIN(::HeldOrderTest::Built)
    VKM_F(step)
VKM_REFLECT_END()

namespace {

// A project keeps its settings in a file of its own, a key a field: what it wrote comes back,
// a field the file lacks keeps its default, and one it holds wrongly costs that field alone.
void testAProjectsOwnFileRoundTrips() {
    std::printf("A project's own settings, through ReflectJson:\n");
    using namespace ReflectJsonTest;
    Settings saved;
    saved.volume  = 0.25f;
    saved.vsync   = false;
    saved.name    = "Vilimir";
    saved.quality = Quality::Low;
    saved.tint    = {0.5f, 0.25f, 1.0f};
    saved.keys    = {{"jump", 32}, {"dash", 340}};
    const nlohmann::json doc = ReflectJson::toJson(saved);
    check(
        "it writes a key a field, an enum by its name",
        doc.value("quality", std::string()) == "Low" && doc.contains("tint")
    );

    Settings back;
    ReflectJson::fromJson(doc, back);
    const bool same = back.volume == 0.25f && !back.vsync && back.name == "Vilimir"
        && back.quality == Quality::Low && back.tint == saved.tint && back.keys.size() == 2
        && back.keys[1].action == "dash" && back.keys[1].code == 340;
    check("  and reads all of it back", same);

    nlohmann::json older = doc;
    older.erase("vsync");
    older["volume"] = "loud";
    Settings partly;
    partly.vsync = true;
    ReflectJson::fromJson(older, partly);
    check("  a field it lacks keeps its default", partly.vsync);
    check(
        "  one it holds wrongly keeps its own, the rest load",
        partly.volume == 0.8f && partly.name == "Vilimir"
    );

    // A list kept whole: one that is no list, or wrong anywhere, keeps the one there was.
    const nlohmann::json notAList = 5;
    const nlohmann::json wrongInside = nlohmann::json::parse(
        R"([{"action": "fire", "code": 9}, {"action": "aim", "code": "x"}])"
    );
    const nlohmann::json noObject = nlohmann::json::parse(R"([{"action": "fire", "code": 9}, 5])");
    for (const nlohmann::json& wrong : {notAList, wrongInside, noObject}) {
        nlohmann::json listed = doc;
        listed["keys"] = wrong;
        Settings held = back;
        ReflectJson::fromJson(listed, held);
        check(
            ("  a list wrong as " + wrong.dump() + " keeps the one there was").c_str(),
            held.keys.size() == 2 && held.keys[1].action == "dash"
        );
    }
}

// The scene file is not a project's own: a field of the wrong type there fails the load, which
// names where it stood, and the scene loaded into is left as it was.
void testAMistypedSceneFieldFailsTheLoad() {
    std::printf("A scene with one component field of the wrong type:\n");
    Scene scene;
    ResourceManager resources;
    const EntityId entity = scene.createEntity();
    scene.add<Transform>(entity, Transform{});
    scene.add<Light>(entity, Light{});
    nlohmann::json doc = nlohmann::json::parse(SceneSerializer::saveToString(scene, resources));
    doc["entities"][0]["components"]["Light"]["intensity"] = "bright";

    Scene live;
    const EntityId kept = live.createEntity();
    live.add<Transform>(kept, Transform{});
    const bool loaded = SceneSerializer::loadFromString(doc.dump(), live, resources);
    check("the load fails", !loaded);
    const bool untouched = live.isAlive(kept) && live.has<Transform>(kept);
    check("  and the scene it was loading into is as it was", untouched);

    // A list that is no list is a field of the wrong type too, not an empty list.
    scene.add<LOD>(entity, LOD{});
    nlohmann::json listed = nlohmann::json::parse(SceneSerializer::saveToString(scene, resources));
    listed["entities"][0]["components"]["LOD"]["levels"] = 5;
    const bool listLoaded = SceneSerializer::loadFromString(listed.dump(), live, resources);
    check("a list that is no list fails the load too", !listLoaded);
}

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

// A host may set a locale that writes a decimal comma - a file dialog, a library - and a number
// tidied for the file must read back as the number it was, whatever the locale says.
void testANumberIsTidiedAlikeUnderADecimalComma() {
    std::printf("A number tidied under a locale with a decimal comma:\n");

    const std::string held = std::setlocale(LC_NUMERIC, nullptr);
    bool comma = false;
    for (const char* name : {"bg_BG.UTF-8", "bg_BG.utf8", "de_DE.UTF-8", "de_DE.utf8", "bg-BG", "de-DE"}) {
        if (std::setlocale(LC_NUMERIC, name)) {
            comma = true;
            break;
        }
    }
    if (!comma) {
        std::printf("      no locale with a decimal comma here; skipped\n");
        return;
    }
    const std::filesystem::path path = runScratch() / "vkm_locale_numbers.json";
    nlohmann::json doc = {{"x", static_cast<double>(0.1f)}};
    const bool written = writeOwnFile(path, doc);
    std::setlocale(LC_NUMERIC, held.c_str());
    nlohmann::json back;
    std::ifstream(path) >> back;
    check("it is written as the number it was", written && back["x"].get<double>() == 0.1);
    std::error_code ec;
    std::filesystem::remove(path, ec);
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

// An emitter's curves, gradient and ranges are lists and pairs, and its enums are names: each
// comes back as it went, and none takes the default it was loaded over.
void testAnEmitterRoundTrips() {
    std::printf("Round-tripping an emitter and a sub-emitter's trigger:\n");

    ResourceManager resources;
    ParticleEmitter before;
    before.shape             = EmitShape::Box;
    before.blend             = ParticleBlend::Distortion;
    before.render            = ParticleRender::Trail;
    before.seenBy            = SeenBy::Viewmodel;
    before.lifetime          = {0.5f, 2.5f};
    before.rotation          = {-1.0f, 1.0f};
    before.sizeOverLife.keys = {{0.0f, 0.5f}, {0.3f, 2.0f}, {1.0f, 0.0f}};
    before.color.keys        = {{0.0f, {0.1f, 0.2f, 0.3f, 0.4f}}};
    before.angle             = 0.75f;

    const nlohmann::json saved = ComponentSerializer::save(before, resources);
    check("its enums are written by name", saved.dump().find("\"Distortion\"") != std::string::npos);
    ParticleEmitter after;
    ComponentSerializer::load(saved, after, resources);
    const bool enums = after.shape == EmitShape::Box && after.blend == ParticleBlend::Distortion
        && after.render == ParticleRender::Trail && after.seenBy == SeenBy::Viewmodel;
    check("  and read back", enums);
    check(
        "its ranges come back",
        after.lifetime.min == 0.5f && after.lifetime.max == 2.5f
            && after.rotation.min == -1.0f && after.angle == 0.75f
    );
    const auto& keys = after.sizeOverLife.keys;
    check("  its curve, key for key", keys.size() == 3 && keys[1].time == 0.3f && keys[1].value == 2.0f);
    check(
        "  and its gradient, the default's two keys gone",
        after.color.keys.size() == 1 && after.color.keys[0].color == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)
    );

    ParticleTrigger trigger{ParticleEvent::Collision, 7};
    ParticleTrigger loaded;
    ComponentSerializer::load(ComponentSerializer::save(trigger), loaded);
    const bool kept = loaded.event == ParticleEvent::Collision && loaded.count == 7;
    check("a trigger keeps its event and count", kept);
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

    // List order is run order, so a held entry remembers how many constructed ones were listed ahead of it.
    check(
        "  remembering where each one sat",
        sc.unknown.size() == 2 && sc.unknown[0].constructedBefore == 0 && sc.unknown[1].constructedBefore == 0
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

    // Behind more constructed ones than the list holds, they go at its end, in order, none lost.
    ScriptComponent gapped;
    gapped.unknown.push_back({"Kept", "{}", 1});
    gapped.unknown.push_back({"AlsoKept", "{}", 2});
    const nlohmann::json closed = ComponentSerializer::save(gapped);
    check(
        "  and one whose place before it emptied is kept, in order",
        closed["behaviors"].size() == 2
            && closed["behaviors"][0]["type"] == "Kept"
            && closed["behaviors"][1]["type"] == "AlsoKept"
    );

    // Open, save, open again: some losses only show on the second pass.
    ScriptComponent reloaded;
    ComponentSerializer::load(again, reloaded);
    check("  through any number of saves", ComponentSerializer::save(reloaded) == authored);
}

// A held behavior keeps its place among the constructed ones: one removed ahead of it does not
// carry it past the one behind it.
void testAHeldBehaviorKeepsItsPlaceWhenOneAheadGoes() {
    std::printf("A behavior whose module is missing, between two that run:\n");

    BehaviorRegistry::get().registerBehaviors<HeldOrderTest::Built>();
    const nlohmann::json behaviors = nlohmann::json::array({
        {{"type", "Built"}, {"properties", {{"step", 0}}}},
        {{"type", "Missing"}, {"properties", nlohmann::json::object()}},
        {{"type", "Built"}, {"properties", {{"step", 2}}}},
    });
    const nlohmann::json authored = {{"behaviors", behaviors}};
    ScriptComponent sc;
    ComponentSerializer::load(authored, sc);
    check("the two that run are built", sc.behaviors.size() == 2);
    check("  and the missing one held", sc.unknown.size() == 1);

    removeBehavior(sc, 0);
    const nlohmann::json saved = ComponentSerializer::save(sc)["behaviors"];
    check(
        "with the first removed, the held one still runs ahead of the last",
        saved.size() == 2
            && saved[0]["type"] == "Missing"
            && saved[1]["type"] == "Built"
            && saved[1]["properties"]["step"] == 2
    );
    BehaviorRegistry::get().clear();
}

void testAProjectSurvivesBeingWritten() {
    std::printf("What a project.json keeps when a tool writes it back:\n");

    const std::filesystem::path root =
        runScratch() / "vkm_project_write_test";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    // Hand-authored, with a key this build does not know and splash and font lists holding
    // entries loadProject skips.
    {
        std::ofstream out(root / "project.json");
        out << R"({
    "name": "Written By Hand",
    "engineVersion": "0.0.1",
    "entryScene": "scenes/start.json",
    "tickRate": 90,
    "maxPlayers": 3,
    "netPort": 27888,
    "physicsLayers": ["Default", "Ragdoll", 2, "Water"],
    "splash": ["not an entry", {"image": 7}, {"image": "logo.png", "seconds": 2.0}],
    "fonts": [3, {"name": "game:display", "file": "fonts/display.ttf"}, {"name": "game:nameless"}],
    "loading": {"tips": ["Jump with Space", 3, "", "Duck"], "screen": "prefabs/load.json", "water": false},
    "somethingThisBuildDoesNotKnow": {"kept": true}
})";
    }

    Project project;
    check("a hand-authored project reads", loadProject(root, project));
    check("with the seats it names", project.maxPlayers == 3);
    check("and the port", project.netPort == 27888);
    check(
        "and the names of its physics layers, by bit, one that is no name left unnamed in its place",
        project.physicsLayers.size() == 4 && project.physicsLayers[1] == "Ragdoll"
            && project.physicsLayers[2].empty() && project.physicsLayers[3] == "Water"
    );
    const bool oneFont = project.fonts.size() == 1;
    check(
        "and the fonts that name a file, by name",
        oneFont && project.fonts[0].name == "game:display" && project.fonts[0].file == "fonts/display.ttf"
    );
    check("  past entries read wrong", project.splash.size() == 1 && project.splash[0].image == "logo.png");
    check(
        "and its loading screen, past tips that are none",
        project.loading.tips == std::vector<std::string>({"Jump with Space", "Duck"})
            && project.loading.screen == "prefabs/load.json" && !project.loading.water
    );

    project.maxPlayers = 8;
    project.physicsLayers.push_back("");  // a trailing blank, as an editor's emptied row leaves
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
            && reread.physicsLayers == std::vector<std::string>({"Default", "Ragdoll", "", "Water"})
    );

    // What the writer must not lose: an unmodelled key, and a list left alone, entries the read
    // skipped and all.
    nlohmann::json doc;
    {
        std::ifstream in(root / "project.json");
        in >> doc;
    }
    check("a key this build does not model is still there", doc.contains("somethingThisBuildDoesNotKnow"));
    check(
        "and the splash list it did not edit",
        doc.contains("splash") && doc["splash"].is_array() && doc["splash"].size() == 3
    );
    check("and the font list, as it was", doc.contains("fonts") && doc["fonts"].size() == 3);
    check("and the loading screen's tips", doc.contains("loading") && doc["loading"]["tips"].size() == 4);

    // An edited list is written as the editor holds it.
    Project fonted = reread;
    fonted.fonts.push_back({"game:title", "fonts/title.ttf"});
    fonted.splash.clear();
    fonted.loading = LoadingScreen{};
    check("an added font writes", saveProject(root, fonted));
    Project withFont;
    check("  and reads again", loadProject(root, withFont) && withFont.fonts == fonted.fonts);
    check("  a cleared splash list too", withFont.splash.empty());
    {
        std::ifstream in(root / "project.json");
        nlohmann::json cleared;
        in >> cleared;
        check("  and a loading screen back to the engine's leaves no block", !cleared.contains("loading"));
    }

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

// A reference to an entity since deleted names nothing: its slot goes to the next entity made,
// which would otherwise take it over when the scene is read back.
void testAReferenceToADeletedEntityNamesNothing() {
    std::printf("A joint whose other end was deleted, its slot taken again:\n");

    Scene scene;
    ResourceManager resources;
    const EntityId beam = scene.createEntity();
    scene.add<Transform>(beam, Transform{});
    const EntityId weight = scene.createEntity();
    scene.add<Transform>(weight, Transform{});
    scene.add(weight, makeName("Weight"));
    Joint rope;
    rope.connected = beam;
    scene.add<Joint>(weight, std::move(rope));

    scene.destroyEntity(beam);
    const EntityId newcomer = scene.createEntity();
    scene.add<Transform>(newcomer, Transform{});
    scene.add(newcomer, makeName("Newcomer"));
    check("the newcomer takes the deleted end's slot", newcomer.slot() == beam.slot());

    Scene back;
    const std::string written = SceneSerializer::saveToString(scene, resources);
    const bool        loaded  = SceneSerializer::loadFromString(written, back, resources);
    check("  the scene reads back", loaded);
    EntityId loadedWeight{};
    back.forEach<Name>([&](EntityId id, Name& n) {
        if (std::string(n.value) == "Weight") loadedWeight = id;
    });
    check("  and the joint names nothing", loadedWeight && !back.get<Joint>(loadedWeight).connected);
}

// A top-level key of the wrong type costs that key alone: the read goes on to the rest, which a
// tool saving the file after would otherwise write over with defaults. A file that does not parse
// is not written over at all.
void testAMistypedProjectKeyCostsOneKey() {
    std::printf("A project.json with a top-level key of the wrong type:\n");

    const ScratchProject project("vkm_project_mistyped_key");
    const std::filesystem::path file = project.root() / "project.json";
    {
        std::ofstream out(file);
        out << R"({
    "name": "Mistyped Key",
    "version": 1,
    "entryScene": "scenes/start.json",
    "tickRate": 90,
    "fonts": [{"name": "game:display", "file": "fonts/display.ttf"}]
})";
    }
    Project loaded;
    check("the project still reads", loadProject(project.root(), loaded));
    check("  the mistyped key keeps its default", loaded.version.empty());
    const bool rest = loaded.entryScene == "scenes/start.json" && loaded.tickRate == 90;
    check("  and every key after it is read", rest);

    RenderSettings tuned;
    tuned.exposure = 2.5f;
    check("its render settings write alone", saveProjectRender(project.root(), tuned));
    Project again;
    check("  and read back", loadProject(project.root(), again) && nearly(again.render.exposure, 2.5f));
    check("  leaving the rest as it was", again.entryScene == "scenes/start.json" && again.fonts.size() == 1);

    {
        std::ofstream out(file, std::ios::trunc);
        out << "{ \"name\": \"Broken\", ";
    }
    check("a file that does not parse is not saved over", !saveProject(project.root(), again));
    check("  nor its render settings", !saveProjectRender(project.root(), tuned));
    std::ifstream in(file);
    const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check("  and it is left as it was", kept == "{ \"name\": \"Broken\", ");
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

    check("a directory holding project.json is one", !findProjectRoot(root).empty() && isProjectRoot(root));
    // Paths inside count, so an author can drop a scene file on the editor and have it
    // find its project.
    check("  and so is anything inside it", !findProjectRoot(root / "scenes").empty());
    check(
        "  which resolves to the directory itself, not the child",
        findProjectRoot(root / "scenes") == findProjectRoot(root)
    );

    // Moved to this engine, it names it, and keeps every other key it held.
    check("it moves to this engine", saveProjectEngine(root));
    Project moved;
    loadProject(root, moved);
    const bool named = !moved.engineVersion.empty() && compatibleEngine(moved.engineVersion);
    check("  naming it, and keeping the rest", named && moved.name == "Probe");

    std::filesystem::remove_all(root, ec);
}

// A project is called what project.json names it, else what its folder is called - also when
// the path it is opened by ends in a separator, whose last component is empty.
void testAProjectIsNamedByItsFileOrItsFolder() {
    std::printf("What a project is called:\n");

    const std::filesystem::path root = runScratch() / "vkm_project_named";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    Project bare;
    loadProject(root, bare);
    check("a directory with no project.json takes its folder's name", bare.name == "vkm_project_named");

    {
        std::ofstream out(root / "project.json");
        out << R"({"name":""})";
    }
    Project nameless;
    check("a project.json naming nothing reads", loadProject(root / "", nameless));
    check("  and takes its folder's name, past a trailing separator", nameless.name == "vkm_project_named");

    {
        std::ofstream out(root / "project.json");
        out << R"({"name":"Probe"})";
    }
    Project named;
    loadProject(root, named);
    check("one naming itself keeps its name", named.name == "Probe");

    std::filesystem::remove_all(root, ec);
}

void testTheSceneOfRecordStillOpens() {
    std::printf("The scene the engine keeps as its record of the format:\n");

    // engine.md names obstacle_course.json the scene of record that a format change
    // must round-trip; this does it here rather than in front of an author.
    const std::filesystem::path scene =
        std::filesystem::path(VKM_EXAMPLES_DIR) / "the_lab" / "scenes" / "obstacle_course.json";

    std::error_code ec;
    if (!std::filesystem::exists(scene, ec)) {
        // A packaged SDK ships no examples/.
        std::printf("      (no examples/ in this build - skipped)\n");
        return;
    }

    // No asset factories here, so every named mesh and material is unresolved and says
    // so; unresolved references are kept, so these errors are the format working.
    std::printf("      (the unresolved-asset errors below are expected: no factories here)\n");

    // Its project's root, so the prefabs it places are built as an author's load builds them.
    const std::filesystem::path previousRoot = ProjectPaths::projectRoot();
    ProjectPaths::setProjectRoot(scene.parent_path().parent_path());

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

    // engine.md 5.1: the file is in the form the writer produces, so the save says what it read.
    // The assets block is the exception here: with no cooked asset or project module loaded to
    // resolve a name, it holds only the names a field gives, each one the file's.
    nlohmann::json authored;
    nlohmann::json written;
    const bool bothRead = detail::readJsonFile(scene, authored, "Scene")
        && detail::readJsonFile(echoed, written, "Scene");
    check("  and both files read as JSON", bothRead);
    if (bothRead) {
        bool namesKnown = written["assets"].is_object();
        for (const auto& [kind, entries] : written["assets"].items()) {
            const nlohmann::json listed = authored["assets"].value(kind, nlohmann::json::array());
            for (const nlohmann::json& entry : entries) {
                if (std::find(listed.begin(), listed.end(), entry) == listed.end()) namesKnown = false;
            }
        }
        authored.erase("assets");
        written.erase("assets");
        check("  saying back exactly what the file says", written == authored);
        check("  and naming no asset the file does not", namesKnown);
    }

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
    ProjectPaths::setProjectRoot(previousRoot);
}

// Each example names this engine exactly: a bump that leaves one behind names an older engine,
// and one of another release opens as made for another engine.
void testEveryExampleNamesThisEngine() {
    std::printf("Every example's project.json, against this engine:\n");

    std::error_code ec;
    const std::filesystem::path examples(VKM_EXAMPLES_DIR);
    if (!std::filesystem::is_directory(examples, ec)) {
        std::printf("      (no examples in this build - skipped)\n");
        return;
    }
    // What this engine writes as its name, read back from a scratch project it was written into.
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "vkm_engine_name";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch, ec);
    std::ofstream(scratch / "project.json") << R"({"name": "Probe"})";
    Project written;
    const bool wrote = saveProjectEngine(scratch) && loadProject(scratch, written);
    std::filesystem::remove_all(scratch, ec);

    std::vector<std::string> behind;
    int seen = 0;
    std::filesystem::directory_iterator example(examples, ec), end;
    for (; !ec && example != end; example.increment(ec)) {
        Project project;
        if (!loadProject(example->path(), project)) continue;
        ++seen;
        if (project.engineVersion != written.engineVersion) behind.push_back(project.name);
    }
    for (const std::string& name : behind) std::printf("      %s names another engine\n", name.c_str());
    check("this engine's name is read back", wrote && !written.engineVersion.empty());
    check("  and every example names it", seen > 0 && behind.empty());
}

// The shipped prefabs are in the writer's form too (engine.md 5.1): each is built and saved over
// itself, as the editor's Save as Prefab on an instance does, and the save must say what it read.
// A copy is what gets saved over, so its nextUid seeds the writer as the file's own would.
void testEveryShippedPrefabIsInTheWritersForm() {
    std::printf("Every shipped prefab, built and saved back over itself:\n");

    // Each with the project that holds it, so what it names resolves as in that project.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> prefabs;
    const std::filesystem::path shipped[] = {
        std::filesystem::path(VKM_EXAMPLES_DIR),
        std::filesystem::path(VKM_ENGINE_DIR) / "content"
    };
    for (const std::filesystem::path& projects : shipped) {
        // One error per walk: a folder with no prefabs/ sets it, and must not end the walk above.
        std::error_code ec;
        if (!std::filesystem::is_directory(projects, ec)) continue;
        std::filesystem::directory_iterator project(projects, ec), end;
        for (; !ec && project != end; project.increment(ec)) {
            std::error_code inner;
            const std::filesystem::path folder = project->path() / "prefabs";
            if (!std::filesystem::is_directory(folder, inner)) continue;
            std::filesystem::recursive_directory_iterator it(folder, inner), stop;
            for (; !inner && it != stop; it.increment(inner)) {
                if (it->is_regular_file() && it->path().extension() == ".json") {
                    prefabs.emplace_back(project->path(), it->path());
                }
            }
        }
    }
    if (prefabs.empty()) {
        // A packaged SDK ships no examples/ or content/.
        std::printf("      (no shipped prefabs in this build - skipped)\n");
        return;
    }
    std::sort(prefabs.begin(), prefabs.end());
    std::printf("      (the unresolved-asset errors below are expected: no factories here)\n");

    const std::filesystem::path previousRoot = ProjectPaths::projectRoot();
    const std::filesystem::path echoed       = runScratch() / "vkm_shipped_prefab.json";
    std::vector<std::string> differing;
    std::error_code ec;
    for (const auto& [project, prefab] : prefabs) {
        ProjectPaths::setProjectRoot(project);
        std::filesystem::copy_file(prefab, echoed, std::filesystem::copy_options::overwrite_existing, ec);

        Scene world;
        ResourceManager resources;
        const EntityId root = Prefab::instantiate(world, resources, echoed.string());
        nlohmann::json authored;
        nlohmann::json written;
        const bool echoes = !ec && world.isAlive(root)
            && Prefab::save(world, root, echoed.string(), resources)
            && detail::readJsonFile(prefab, authored, "Prefab")
            && detail::readJsonFile(echoed, written, "Prefab");

        // The assets block names only what a field gives, each one the file's, as for the scene.
        bool same = echoes && written["assets"].is_object();
        if (same) {
            const nlohmann::json named = authored.value("assets", nlohmann::json::object());
            for (const auto& [kind, entries] : written["assets"].items()) {
                const nlohmann::json listed = named.value(kind, nlohmann::json::array());
                for (const nlohmann::json& entry : entries) {
                    if (std::find(listed.begin(), listed.end(), entry) == listed.end()) same = false;
                }
            }
            authored.erase("assets");
            written.erase("assets");
            same = same && written == authored;
        }
        if (!same) differing.push_back(prefab.lexically_relative(project.parent_path()).generic_string());
    }
    std::filesystem::remove(echoed, ec);
    ProjectPaths::setProjectRoot(previousRoot);

    std::printf("      %zu prefab(s)\n", prefabs.size());
    for (const std::string& one : differing) {
        std::printf("      %s\n", one.c_str());
    }
    check("each one's save says back exactly what its file says", differing.empty());
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
// "" - inside a list as well, addressed by the element's index, or opening a scene
// with its library missing and saving it would empty every LOD level.
void testAnUnresolvedArrayElementIsKept() {
    std::printf("A missing asset named from inside an array:\n");

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
        if (ref.name == "gone:named")    named = ref.field == "mesh";
        if (ref.name == "gone:in_array") fromArray = ref.component == "LOD" && ref.field == "levels.0.mesh";
    }
    check("  the one in a named field is kept, so a save can put it back", named);
    check("  and the one from inside an array, under its element", fromArray);

    nlohmann::json saved = nlohmann::json::object();
    SceneSerializer::saveComponents(into, loaded, saved, empty);
    check("  which a save writes back there", saved["LOD"]["levels"][0]["mesh"] == "gone:in_array");
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

// A settings screen's one quality choice: each tier at least the one under it, and what the game
// looks like left as the project set it.
void testAGraphicsPresetSetsWhatCostsAndKeepsTheLook() {
    std::printf("A graphics preset:\n");
    RenderSettings project;
    project.exposure      = 1.5f;
    project.tonemap       = Tonemap::AgX;
    project.bloomStrength = 0.2f;
    const RenderSettings low   = withPreset(project, GraphicsPreset::Low);
    const RenderSettings ultra = withPreset(RenderSettings{}, GraphicsPreset::Ultra);
    check(
        "low turns the costly passes off",
        low.msaaSamples == 1 && !low.gtao && !low.ssr && !low.bloom && !low.softShadows
    );
    check(
        "  and renders the scene at fewer pixels, never past the floor",
        low.renderScale < 1.0f && low.renderScale >= RenderSettings::MIN_RENDER_SCALE
    );
    check(
        "  ultra has them all, at the most",
        ultra.msaaSamples == 8 && ultra.gtao && ultra.ssr && ultra.shadowResolution == 4096
            && ultra.softShadows && ultra.renderScale == 1.0f && !ultra.ssrHalfResolution
    );
    check("  each sample count one the renderer takes", RenderSettings::isMsaaSampleCount(low.msaaSamples));
    check(
        "  the look kept",
        low.exposure == 1.5f && low.tonemap == Tonemap::AgX && low.bloomStrength == 0.2f
    );
    bool rising = true;
    RenderSettings under = withPreset(RenderSettings{}, GraphicsPreset::Low);
    for (int tier = 1; tier < static_cast<int>(GraphicsPreset::Count); ++tier) {
        const RenderSettings next = withPreset(RenderSettings{}, static_cast<GraphicsPreset>(tier));
        rising = rising && next.msaaSamples >= under.msaaSamples
            && next.shadowResolution >= under.shadowResolution && next.renderScale >= under.renderScale
            && (next.softShadows || !under.softShadows)
            && (!next.ssrHalfResolution || under.ssrHalfResolution);
        under = next;
    }
    check("  each tier at least the one under it", rising);

    // A resolution chosen on a settings screen, as a scale of the viewport it fills.
    check(
        "a render resolution is the share of the viewport's height, within the scale's range",
        std::abs(RenderSettings::renderScaleFor(720, 1080) - 2.0f / 3.0f) < 1e-6f
            && RenderSettings::renderScaleFor(2160, 1080) == 1.0f
            && RenderSettings::renderScaleFor(360, 1080) == RenderSettings::MIN_RENDER_SCALE
            && RenderSettings::renderScaleFor(720, 0) == 1.0f
    );

    // An effect the project turned off for its look stays off at any tier.
    RenderSettings plain;
    plain.bloom = false;
    plain.softShadows = false;
    const RenderSettings plainUltra = withPreset(plain, GraphicsPreset::Ultra);
    check("an effect the project turned off stays off", !plainUltra.bloom && !plainUltra.softShadows);
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

// "Uses a material" is one rule wherever an editor asks: a material an entity's component names,
// by handle or by name, and one the terrain it places holds in a layer or a plant.
void testWhatDrawsAMaterialIsOneRule() {
    std::printf("Which entities draw a material:\n");
    Scene           scene;
    ResourceManager resources;
    const MaterialHandle stone = resources.add(MaterialAsset{}, "test:stone");
    const MaterialHandle grass = resources.add(MaterialAsset{}, "test:grass");
    TerrainAsset ground;
    ground.layers[1] = stone;
    TerrainFoliage meadow;
    meadow.parts.push_back({MeshHandle{}, grass, glm::mat4(1.0f), {}, {}});
    ground.foliage.push_back(meadow);
    const TerrainHandle terrain = resources.add(std::move(ground), "test:ground");

    const EntityId placed = scene.createEntity();
    scene.add(placed, Transform{});
    scene.add(placed, Terrain{terrain});
    const EntityId drawn = scene.createEntity();
    scene.add(drawn, Transform{});
    Mesh mesh;
    mesh.material = grass;
    scene.add(drawn, std::move(mesh));

    const auto draws = [&](EntityId id, MaterialHandle material) {
        AssetSerializer::EntityAssetRefs refs;
        AssetSerializer::collectAssetRefs(scene, id, resources, refs);
        bool       found = false;
        const auto match = [&](MaterialHandle m) { found = found || m == material; };
        AssetSerializer::forEachMaterialOf(refs, resources, match);
        return found;
    };
    check("a mesh draws its material", draws(drawn, grass) && !draws(drawn, stone));
    check("  and a terrain its layers' and its plants'", draws(placed, stone) && draws(placed, grass));
}

} // namespace

void runSceneTests() {
    testWhatDrawsAMaterialIsOneRule();
    testAFileTheEngineWritesIsOneItCanReadBack();
    testAProjectsOwnFileRoundTrips();
    testAMistypedSceneFieldFailsTheLoad();
    testASceneWithANonFiniteNumberStillRoundTrips();
    testANumberIsTidiedAlikeUnderADecimalComma();
    testImportedHierarchy();
    testComponentRoundTrip();
    testAnEmitterRoundTrips();
    testSceneRoundTripKeepsReferences();
    testAReferenceToADeletedEntityNamesNothing();
    testUnknownBehaviorsSurviveASave();
    testAHeldBehaviorKeepsItsPlaceWhenOneAheadGoes();
    testAProjectSurvivesBeingWritten();
    testAMistypedRenderSettingCostsOneSetting();
    testAMistypedProjectKeyCostsOneKey();
    testAuthoredValuesTheRendererDividesByAreBounded();
    testWhatIsAndIsNotAProject();
    testAProjectIsNamedByItsFileOrItsFolder();
    testTheSceneOfRecordStillOpens();
    testEveryShippedPrefabIsInTheWritersForm();
    testEveryExampleNamesThisEngine();
    testAssetHandlesSurviveARoundTrip();
    testAnUnresolvedArrayElementIsKept();
    testAKeptNameSurvivesAFillThatIsTakenBack();
    testTheRenderTuningSurvivesASave();
    testAGraphicsPresetSetsWhatCostsAndKeepsTheLook();
}
