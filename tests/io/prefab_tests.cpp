#include "support.h"

#include <nlohmann/json.hpp>

#include "command/prefab_overrides.h"
#include "io/scene/prefab.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/light.h"
#include "ecs/hierarchy_operations.h"

namespace {

// No prefab ships in the tree, so these drive save, instantiate and the override
// entry points against a scratch project root.

EntityId posed(Scene& scene, const char* name, const glm::vec3& at) {
    const EntityId id = scene.createEntity();
    scene.add(id, makeName(name));
    Transform t;
    t.position = at;
    scene.add<Transform>(id, std::move(t));
    return id;
}

// root -> child -> grandchild, a metre apart, so a rebuilt instance reads by shape.
EntityId buildChain(Scene& scene) {
    const EntityId root  = posed(scene, "Root",  {0.0f, 0.0f, 0.0f});
    const EntityId child = posed(scene, "Child", {1.0f, 0.0f, 0.0f});
    const EntityId grand = posed(scene, "Grand", {2.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(scene, child, root);
    HierarchyOperations::setParent(scene, grand, child);
    return root;
}

size_t countChildren(const Scene& scene, EntityId parent) {
    size_t n = 0;
    HierarchyOperations::forEachChild(scene, parent, [&](EntityId) { ++n; });
    return n;
}

// The override fixture. A Light holds every shape sameShape has a rule for - number,
// fixed-width vector, bool, enum - and the Collider adds the last: an array of
// objects, the only array whose length is not part of its type.
EntityId buildLamp(Scene& scene) {
    const EntityId post = posed(scene, "Post", {0.0f, 0.0f, 0.0f});
    const EntityId lamp = posed(scene, "Lamp", {0.0f, 3.0f, 0.0f});

    Light light;
    light.type      = LightType::Point;
    light.intensity = 3.0f;
    light.color     = {1.0f, 1.0f, 1.0f};
    scene.add(lamp, std::move(light));
    scene.add(lamp, Collider{});

    HierarchyOperations::setParent(scene, lamp, post);
    return post;
}

// Builds an instance with overrides on a root it creates, as SceneSerializer::load
// does; the instantiate() overloads take no overrides.
EntityId instanceWith(
    Scene& scene,
    ResourceManager& resources,
    const std::string& path,
    const std::vector<PrefabOverride>& overrides,
    std::set<std::string>* drift
) {
    const EntityId root = scene.createEntity();
    if (!Prefab::instantiateInto(scene, resources, path, root, overrides, drift)) {
        HierarchyOperations::destroyHierarchy(scene, root);
        return {};
    }
    PrefabInstance instance;
    instance.source    = path;
    instance.overrides = overrides;
    scene.add(root, std::move(instance));
    return root;
}

EntityId entityWithUid(const Scene& scene, EntityId root, uint32_t uid) {
    return HierarchyOperations::findInSelfOrDescendantsIf(scene, root, [&](EntityId id) {
        const PrefabEntity* stamped = scene.tryGet<PrefabEntity>(id);
        return stamped && stamped->uid == uid;
    });
}

// One prose line per refusal, addressed like the override: "on entity 1
// (Light.color): type does not match the prefab's". Matched on address and reason,
// since several rules end in the same words.
bool refused(const std::set<std::string>& drift, uint32_t uid, const char* where, const char* reason) {
    const std::string tail =
        "on entity " + std::to_string(uid) + " (" + where + "): " + reason;
    for (const std::string& line : drift) {
        if (line.find(tail) != std::string::npos) return true;
    }
    return false;
}

void testASubtreeSurvivesBeingWrittenDownAndBuiltAgain() {
    std::printf("A prefab written out and instantiated back:\n");

    ScratchProject dir("vkm_prefab_tests");
    ResourceManager resources;

    Scene authored;
    const EntityId root = buildChain(authored);
    const std::string path = "prefabs/chain.json";
    check("the subtree saves", Prefab::save(authored, root, path, resources));

    Scene target;
    const EntityId built = Prefab::instantiate(target, resources, path);
    check("and instantiates into another scene", static_cast<bool>(built));

    check("  the root is an instance", target.has<PrefabInstance>(built));
    check("  with one child", countChildren(target, built) == 1);

    EntityId child{};
    HierarchyOperations::forEachChild(target, built, [&](EntityId id) { child = id; });
    check("  whose own child is under it", child && countChildren(target, child) == 1);

    EntityId grand{};
    HierarchyOperations::forEachChild(target, child, [&](EntityId id) { grand = id; });
    check("  and the whole chain is three entities deep", static_cast<bool>(grand));
    check(
        "  each where it was authored",
        nearly(target.get<Transform>(child).position.x, 1.0f)
            && nearly(target.get<Transform>(grand).position.x, 2.0f)
    );

    // Every entity carries the uid an override addresses it by.
    check(
        "  and every one of them is stamped",
        target.has<PrefabEntity>(built) && target.has<PrefabEntity>(child) && target.has<PrefabEntity>(grand)
    );
}

// Siblings walk (and, for UI, paint) in attach order. The file lists them in that
// order and the build attaches in file order, whatever slots were handed out.
void testAnInstanceKeepsTheOrderOfItsSiblings() {
    std::printf("The order of a prefab's siblings, written out and built again:\n");

    ScratchProject dir("vkm_prefab_tests");
    ResourceManager resources;

    Scene authored;
    const EntityId root = posed(authored, "Root", {0.0f, 0.0f, 0.0f});
    const EntityId a    = posed(authored, "A", {0.0f, 0.0f, 0.0f});
    const EntityId b    = posed(authored, "B", {0.0f, 0.0f, 0.0f});
    const EntityId c    = posed(authored, "C", {0.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(authored, c, root);
    HierarchyOperations::setParent(authored, a, root);
    HierarchyOperations::setParent(authored, b, root);

    const auto order = [](const Scene& scene, EntityId parent) {
        std::string names;
        HierarchyOperations::forEachChild(scene, parent, [&](EntityId child) {
            names += scene.get<Name>(child).value;
        });
        return names;
    };
    check("the authored children are in the order they were attached", order(authored, root) == "CAB");

    const std::string path = "prefabs/siblings.json";
    check("  the subtree saves", Prefab::save(authored, root, path, resources));
    Scene target;
    const EntityId built = Prefab::instantiate(target, resources, path);
    check("  and an instance of it has them in the same order", built && order(target, built) == "CAB");
}

// The file is hand-editable, so the loader alone stands between a mistyped file and
// a half-built instance. A link the walk cannot follow must refuse the file whole: an
// entity without a Hierarchy reads as "not part of the instance", so the scene would
// save it inline beside the instance, and the next load would hold both copies.
void testAPrefabThatDescribesNothingIsRefusedWhole() {
    std::printf("A prefab file that has been edited into nonsense:\n");

    ScratchProject dir("vkm_prefab_tests");
    ResourceManager resources;

    Scene authored;
    const EntityId root = buildChain(authored);
    check("the good one saves", Prefab::save(authored, root, "prefabs/good.json", resources));

    const std::filesystem::path file = dir.root() / "prefabs" / "good.json";
    nlohmann::json good;
    {
        std::ifstream in(file);
        in >> good;
    }

    const auto writeAs = [&](const char* name, const nlohmann::json& doc) {
        const std::string rel = std::string("prefabs/") + name;
        std::ofstream out(dir.root() / rel);
        out << doc.dump(2);
        out.close();
        return rel;
    };

    Scene target;
    check("and builds", static_cast<bool>(Prefab::instantiate(target, resources, "prefabs/good.json")));

    // A child naming a later entry: readPrefab takes a parent only from earlier ones.
    nlohmann::json forward = good;
    forward["entities"][1]["parent"] = 2;
    check(
        "a child naming a later entry is refused",
        !Prefab::instantiate(target, resources, writeAs("forward.json", forward))
    );

    nlohmann::json self = good;
    self["entities"][1]["parent"] = 1;
    check("  and one naming itself", !Prefab::instantiate(target, resources, writeAs("self.json", self)));

    nlohmann::json orphan = good;
    orphan["entities"][1].erase("parent");
    check(
        "  and one with no parent named at all",
        !Prefab::instantiate(target, resources, writeAs("orphan.json", orphan))
    );

    // Two entries on one uid: an override would address both and neither.
    nlohmann::json twins = good;
    twins["entities"][2]["uid"] = twins["entities"][1]["uid"];
    check(
        "  and two entities wearing one uid",
        !Prefab::instantiate(target, resources, writeAs("twins.json", twins))
    );

    // Refused whole: nothing of a rejected file reaches the scene.
    size_t roots = 0;
    target.forEachEntity([&](EntityId id) {
        const Hierarchy* node = target.tryGet<Hierarchy>(id);
        if (!node || !node->parent) ++roots;
    });
    check("and a refused file leaves nothing behind", roots == 1);
}

// An override patches the prefab's own document, which is its schema. Each rule is
// the difference between a stale value reported and a component loader throwing,
// which abandons the whole instance.
void testAnOverrideThePrefabHasNoHomeForIsReportedNotApplied() {
    std::printf("Overrides merged into a prefab that has moved on:\n");

    ScratchProject dir("vkm_prefab_tests");
    ResourceManager resources;

    Scene authored;
    const EntityId post = buildLamp(authored);
    const std::string path = "prefabs/lamp.json";
    check("the lamp saves", Prefab::save(authored, post, path, resources));

    EntityId authoredLamp{};
    HierarchyOperations::forEachChild(authored, post, [&](EntityId id) { authoredLamp = id; });
    const uint32_t lampUid = authored.get<PrefabEntity>(authoredLamp).uid;
    check("  and its child is stamped with a uid of its own", lampUid != PrefabEntity::ROOT);

    // The prefab's own Collider block, so the array cases come from the file rather
    // than a second copy of ColliderPart's shape.
    nlohmann::json doc;
    {
        std::ifstream in(dir.root() / "prefabs" / "lamp.json");
        in >> doc;
    }
    nlohmann::json parts;
    for (const nlohmann::json& entry : doc["entities"]) {
        if (entry.value("uid", 0u) == lampUid) parts = entry["components"]["Collider"]["parts"];
    }
    check("  and the file describes its collider", parts.is_array() && parts.size() == 1);

    nlohmann::json twoParts = parts;
    twoParts.push_back(parts[0]);
    nlohmann::json shortPart = parts;
    shortPart[0].erase(shortPart[0].begin());

    const std::vector<PrefabOverride> fitting = {
        {lampUid, "Light",    "intensity", "9.5"},
        {lampUid, "Light",    "color",     "[0.0,1.0,0.0]"},
        {lampUid, "Light",    "type",      "\"Spot\""},
        {lampUid, "Light",    "radius",    "25"},
        {lampUid, "Collider", "parts",     twoParts.dump()},
    };

    Scene target;
    std::set<std::string> fits;
    const EntityId root = instanceWith(target, resources, path, fitting, &fits);
    const EntityId lamp = root ? entityWithUid(target, root, lampUid) : EntityId{};
    const Light* light  = lamp ? target.tryGet<Light>(lamp) : nullptr;

    check("an instance whose overrides still fit builds", light != nullptr);
    check("  with nothing reported", fits.empty());
    check("  the number is the override's", light && nearly(light->intensity, 9.5f));
    check("  and so is the vector", light && nearly(light->color.r, 0.0f) && nearly(light->color.g, 1.0f));
    check("  the enum is read by name", light && light->type == LightType::Spot);
    // An int-valued float writes as an int, so a number is a number.
    check("  an int where the prefab holds a float fits", light && nearly(light->radius, 25.0f));
    // An array of objects is read at whatever length it has, unlike a vec3, whose
    // length is part of its type.
    check(
        "  and a longer list of parts is a longer list",
        lamp && target.get<Collider>(lamp).parts.size() == 2
    );

    // The same instance and file, with every rule broken once.
    const std::vector<PrefabOverride> drifted = {
        {lampUid,            "Light",     "color",     "[0.0,1.0]"},
        {lampUid,            "Light",     "enabled",   "1"},
        {lampUid,            "Collider",  "parts",     shortPart.dump()},
        {lampUid,            "Light",     "beamAngle", "1.0"},
        {lampUid,            "Lantern",   "wick",      "true"},
        {lampUid,            "Light",     "intensity", "not json at all"},
        {lampUid,            "Script",    "behaviors", "[]"},
        {PrefabEntity::ROOT, "Transform", "position",  "[7.0,0.0,0.0]"},
        {4242u,              "Light",     "intensity", "5.0"},
    };

    Scene stale;
    std::set<std::string> drift;
    const EntityId staleRoot = instanceWith(stale, resources, path, drifted, &drift);
    const EntityId staleLamp = staleRoot ? entityWithUid(stale, staleRoot, lampUid) : EntityId{};
    const Light* kept = staleLamp ? stale.tryGet<Light>(staleLamp) : nullptr;
    check("an instance whose overrides have all drifted builds anyway", kept != nullptr);

    check(
        "  a vector of the wrong length is refused",
        refused(drift, lampUid, "Light.color", "type does not match")
    );
    check(
        "  a number where the prefab holds a bool is refused",
        refused(drift, lampUid, "Light.enabled", "type does not match")
    );
    check(
        "  and so is a list whose elements have lost a key",
        refused(drift, lampUid, "Collider.parts", "type does not match")
    );
    check(
        "  a field the prefab has dropped is refused",
        refused(drift, lampUid, "Light.beamAngle", "no such field")
    );
    check(
        "  a component it has dropped is refused",
        refused(drift, lampUid, "Lantern.wick", "no such component")
    );
    check(
        "  a value that is not JSON is refused",
        refused(drift, lampUid, "Light.intensity", "value is not valid JSON")
    );
    // Two rules whatever the prefab says: the root's Transform is the instance's
    // pose, and a Script serializes as one field holding the whole behavior list,
    // so the only address the format can spell replaces it.
    check(
        "  the root's Transform is refused as the instance's own pose",
        refused(
            drift,
            PrefabEntity::ROOT,
            "Transform.position",
            "the root Transform is the instance's own pose"
        )
    );
    check(
        "  a behavior list is refused as the prefab's",
        refused(drift, lampUid, "Script.behaviors", "behavior fields are the prefab's")
    );
    // applyOverrides sees only the file's entities, so one naming an entity the file
    // lacks passes every entity by.
    check(
        "  and an entity the prefab does not have is refused",
        refused(drift, 4242u, "Light.intensity", "no such entity in the prefab")
    );
    check("  each exactly once", drift.size() == drifted.size());

    check(
        "  the component holds what the prefab holds",
        kept && nearly(kept->intensity, 3.0f) && nearly(kept->color.r, 1.0f) && kept->enabled
    );
    check("  the parts list is the prefab's", staleLamp && stale.get<Collider>(staleLamp).parts.size() == 1);
    check(
        "  and the root is where it was placed, not where the override said",
        staleRoot && nearly(stale.get<Transform>(staleRoot).position.x, 0.0f)
    );

    // The other half of the object rule, in its own instance since one override
    // addresses one field: a lost key is caught by absence, a gained one only by count.
    nlohmann::json fatPart = parts;
    fatPart[0]["wobble"] = 1.0;

    Scene fattened;
    std::set<std::string> fatDrift;
    instanceWith(fattened, resources, path, {{lampUid, "Collider", "parts", fatPart.dump()}}, &fatDrift);
    check(
        "  and a list whose elements have gained one is refused as well",
        refused(fatDrift, lampUid, "Collider.parts", "type does not match")
    );
}

// Dropping an override does not undo its edit: the value is the prefab's definition
// patched by the remaining overrides, so giving it back means re-reading that.
// These are what a revert is built on (see PrefabOverrides).
void testWhatThePrefabStillDefinesIsWhatCanBeGivenBack() {
    std::printf("Re-reading one component of an instance from its prefab:\n");

    ScratchProject dir("vkm_prefab_tests");
    ResourceManager resources;

    Scene authored;
    const EntityId post = buildLamp(authored);
    const std::string path = "prefabs/lamp.json";
    check("the lamp saves", Prefab::save(authored, post, path, resources));

    EntityId authoredLamp{};
    HierarchyOperations::forEachChild(authored, post, [&](EntityId id) { authoredLamp = id; });
    const uint32_t lampUid = authored.get<PrefabEntity>(authoredLamp).uid;

    check("the prefab defines the component it wrote", Prefab::definesComponent(path, lampUid, "Light"));
    check("  and not one it never held", !Prefab::definesComponent(path, lampUid, "Camera"));
    check("  and answers for no entity it does not have", !Prefab::definesComponent(path, 4242u, "Light"));

    Scene target;
    const EntityId root = instanceWith(target, resources, path, {}, nullptr);
    const EntityId lamp = root ? entityWithUid(target, root, lampUid) : EntityId{};
    check("an instance of it builds", static_cast<bool>(lamp));

    target.get<Light>(lamp).intensity = 99.0f;
    check(
        "re-reading hands the field the prefab's value back",
        Prefab::reloadComponent(target, resources, path, lamp, lampUid, "Light", {})
            && nearly(target.get<Light>(lamp).intensity, 3.0f)
    );

    const std::vector<PrefabOverride> one = {{lampUid, "Light", "intensity", "12.0"}};
    check(
        "  and with an override still standing, that value patched by it",
        Prefab::reloadComponent(target, resources, path, lamp, lampUid, "Light", one)
            && nearly(target.get<Light>(lamp).intensity, 12.0f)
    );

    check(
        "  a component the prefab does not define cannot be re-read",
        !Prefab::reloadComponent(target, resources, path, lamp, lampUid, "Camera", {})
    );

    // Handing the root the prefab's authored pose would teleport the instance when
    // an override was dropped.
    target.get<Transform>(root).position = {40.0f, 0.0f, 0.0f};
    check(
        "and the root's Transform is refused rather than re-read",
        !Prefab::reloadComponent(target, resources, path, root, PrefabEntity::ROOT, "Transform", {})
            && nearly(target.get<Transform>(root).position.x, 40.0f)
    );
}

// Several readers ask whether an entity belongs to an instance; an edit recorded
// against an entity the saver leaves out is gone on the next load.
void testEveryReaderAgreesWhatAnInstanceHolds() {
    std::printf("Which entities belong to an instance, asked every way it is asked:\n");

    Scene scene;
    const EntityId root  = posed(scene, "Root", glm::vec3(0.0f));
    const EntityId child = posed(scene, "Child", glm::vec3(1.0f, 0.0f, 0.0f));
    const EntityId leaf  = posed(scene, "Leaf", glm::vec3(2.0f, 0.0f, 0.0f));
    const EntityId apart = posed(scene, "Apart", glm::vec3(3.0f, 0.0f, 0.0f));
    scene.add(root, PrefabInstance{"prefabs/thing.json", {}});
    HierarchyOperations::setParent(scene, child, root);
    HierarchyOperations::setParent(scene, leaf, child);

    check("a descendant two levels down resolves to its root", Prefab::instanceRootOf(scene, leaf) == root);
    check("  and the root to itself", Prefab::instanceRootOf(scene, root) == root);
    check("  and an entity outside to nothing", !Prefab::instanceRootOf(scene, apart));

    check(
        "the saver leaves out the descendants",
        Prefab::isInsideInstance(scene, child) && Prefab::isInsideInstance(scene, leaf)
    );
    check("  and keeps the root", !Prefab::isInsideInstance(scene, root));

    check("the editor's overrides resolve the same root", PrefabOverrides::instanceRoot(scene, leaf) == root);
    check(
        "the wire keeps the descendants off it",
        netSilence(scene, leaf) == NetSilence::InsidePrefabInstance
    );
    check("  and names the root", netSilence(scene, root) != NetSilence::InsidePrefabInstance);
}

} // namespace

void runPrefabTests() {
    testASubtreeSurvivesBeingWrittenDownAndBuiltAgain();
    testAnInstanceKeepsTheOrderOfItsSiblings();
    testAPrefabThatDescribesNothingIsRefusedWhole();
    testAnOverrideThePrefabHasNoHomeForIsReportedNotApplied();
    testWhatThePrefabStillDefinesIsWhatCanBeGivenBack();
    testEveryReaderAgreesWhatAnInstanceHolds();
}
