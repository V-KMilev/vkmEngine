// Prefabs: a subtree written once, built in many places, each instance a reference to its file
// with what it changes kept beside it.

#include <string>

#include <nlohmann/json.hpp>

#include "ecs/component/physics/joint.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/prefab_instances.h"
#include "io/project/roots.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;
namespace Ops = Vkm::Engine::HierarchyOperations;

namespace {

constexpr const char* LAMP = "prefabs/lamp.json";

/// A post with a bulb on it, written to LAMP: the root holds a Light of 2, the bulb a Light of 4.
void writeLamp(ResourceManager& resources, float bulb = 4.0f) {
    Scene authoring;
    const EntityId post = authoring.createEntity();
    authoring.add(post, Transform{});
    Light base;
    base.intensity = 2.0f;
    authoring.add(post, base);
    const EntityId top = authoring.createEntity();
    Transform up;
    up.position = {0.0f, 3.0f, 0.0f};
    authoring.add(top, up);
    Light light;
    light.intensity = bulb;
    authoring.add(top, light);
    Ops::setParent(authoring, top, post);
    REQUIRE(Prefab::save(authoring, post, LAMP, resources));
}

Transform placedAt(const glm::vec3& position) {
    Transform t;
    t.position = position;
    return t;
}

/// The one child of @p root.
EntityId childOf(const Scene& scene, EntityId root) {
    EntityId found;
    Ops::forEachChild(scene, root, [&](EntityId child) { found = child; });
    return found;
}

} // namespace

TEST("a prefab builds its subtree where it is placed, and a scene keeps each instance as a reference") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);

    Scene scene;
    const EntityId west = Prefab::instantiate(scene, resources, LAMP, placedAt({-5.0f, 0.0f, 0.0f}));
    const EntityId east = Prefab::instantiate(scene, resources, LAMP, placedAt({5.0f, 0.0f, 0.0f}));
    REQUIRE(west);
    REQUIRE(east);
    CHECK(scene.entityCount() == 4u);
    CHECK(scene.get<Transform>(east).position == glm::vec3(5.0f, 0.0f, 0.0f));
    CHECK(scene.get<Light>(childOf(scene, east)).intensity == 4.0f);
    CHECK(scene.get<PrefabInstance>(east).source == LAMP);

    // Saved, an instance is its file, its pose and nothing of its subtree.
    json doc = json::parse(SceneSerializer::saveToString(scene, resources));
    CHECK(doc["entities"].size() == 2u);
    for (json entry : doc["entities"]) {
        CHECK(entry["prefab"] == LAMP);
        CHECK(!entry["components"].contains("Light"));
    }

    // Edit the file: every instance takes the change at the next load.
    writeLamp(resources, 9.0f);
    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), loaded, resources));
    CHECK(loaded.entityCount() == 4u);
    CHECK(loaded.get<Light>(childOf(loaded, west)).intensity == 9.0f);
    CHECK(loaded.get<Light>(childOf(loaded, east)).intensity == 9.0f);
    CHECK(loaded.get<Transform>(west).position == glm::vec3(-5.0f, 0.0f, 0.0f));

    // A file that will not open leaves the instance, its reference and its entities, empty, not the scene.
    std::filesystem::remove(Roots::resolveProjectPath(LAMP));
    Scene orphaned;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), orphaned, resources));
    CHECK(orphaned.entityCount() == 4u);
    CHECK(!orphaned.has<Light>(childOf(orphaned, west)));
    json kept = json::parse(SceneSerializer::saveToString(orphaned, resources));
    REQUIRE(kept["entities"].size() == 2u);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(kept["entities"][i]["prefab"] == doc["entities"][i]["prefab"]);
        CHECK(kept["entities"][i]["components"]["Transform"] == doc["entities"][i]["components"]["Transform"]);
    }
}

TEST("a reference to an entity inside an instance survives a save and a load") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    Scene scene;
    // A slot freed before the second instance, so the instances' entities do not stand where a
    // load would build them by itself, in the order the file lists the instances.
    const EntityId spare = scene.createEntity();
    REQUIRE(Prefab::instantiate(scene, resources, LAMP, placedAt({-5.0f, 0.0f, 0.0f})));
    scene.destroyEntity(spare);
    const EntityId east  = Prefab::instantiate(scene, resources, LAMP, placedAt({5.0f, 0.0f, 0.0f}));
    const EntityId hinge = scene.createEntity();
    scene.add(hinge, Transform{});
    Joint joint;
    joint.connected = childOf(scene, east);
    scene.add(hinge, joint);
    const std::string path = (scratch() / "hinged.json").string();
    REQUIRE(SceneSerializer::save(scene, resources, path));

    Scene loaded;
    ResourceManager again;
    REQUIRE(SceneSerializer::load(loaded, again, path));
    const EntityId bulb = childOf(loaded, loaded.entityAt(east.slot()));
    REQUIRE(bulb);
    CHECK(loaded.get<Joint>(loaded.entityAt(hinge.slot())).connected == bulb);
    CHECK(countInLog("does not hold") == 0);
}

TEST("an instance whose prefab will not open keeps where its entities stood, and what names one") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    Scene scene;
    const EntityId lamp  = Prefab::instantiate(scene, resources, LAMP, placedAt({0.0f, 0.0f, 0.0f}));
    const EntityId hinge = scene.createEntity();
    scene.add(hinge, Transform{});
    Joint joint;
    joint.connected = childOf(scene, lamp);
    scene.add(hinge, joint);
    const std::string first  = (scratch() / "first.json").string();
    const std::string second = (scratch() / "second.json").string();
    REQUIRE(SceneSerializer::save(scene, resources, first));

    // Loaded and saved again while the prefab is away, then loaded once it is back.
    const std::string file = Roots::resolveProjectPath(LAMP).string();
    std::filesystem::rename(file, file + ".away");
    {
        Scene orphaned;
        ResourceManager none;
        REQUIRE(SceneSerializer::load(orphaned, none, first));
        REQUIRE(SceneSerializer::save(orphaned, none, second));
    }
    std::filesystem::rename(file + ".away", file);
    Scene loaded;
    ResourceManager again;
    REQUIRE(SceneSerializer::load(loaded, again, second));
    const EntityId bulb = childOf(loaded, loaded.entityAt(lamp.slot()));
    REQUIRE(bulb);
    CHECK(loaded.get<Joint>(loaded.entityAt(hinge.slot())).connected == bulb);
}

TEST("a slot an instance's record names that no entity can take is said, and its entity built elsewhere") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    Scene scene;
    const EntityId lamp  = Prefab::instantiate(scene, resources, LAMP, placedAt({0.0f, 0.0f, 0.0f}));
    const EntityId other = scene.createEntity();
    json doc = json::parse(SceneSerializer::saveToString(scene, resources));
    for (json& entry : doc["entities"]) {
        if (entry.contains("prefab")) entry["slots"] = {{"1", other.slot()}, {"head", 3}};
    }

    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), loaded, resources));
    const EntityId bulb = childOf(loaded, loaded.entityAt(lamp.slot()));
    REQUIRE(bulb);
    CHECK(bulb.slot() != other.slot());
    CHECK(countInLog("2 prefab instance slot(s)") == 1);
}

TEST("an override changes one field of one entity in one instance, and survives a save and a load") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    Scene scene;
    const EntityId changed = Prefab::instantiate(scene, resources, LAMP, placedAt({0, 0, 0}));
    const EntityId plain   = Prefab::instantiate(scene, resources, LAMP, placedAt({9, 0, 0}));
    const uint32_t bulb    = scene.get<PrefabEntity>(childOf(scene, changed)).uid;
    scene.get<PrefabInstance>(changed).overrides.push_back({bulb, "Light", "intensity", "6.5"});

    const std::string saved = SceneSerializer::saveToString(scene, resources);
    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(saved, loaded, resources));
    CHECK(loaded.get<Light>(childOf(loaded, changed)).intensity == 6.5f);
    CHECK(loaded.get<Light>(childOf(loaded, plain)).intensity == 4.0f);
    CHECK(loaded.get<Light>(changed).intensity == 2.0f);
    CHECK(SceneSerializer::saveToString(loaded, resources) == saved);
    CHECK(countInLog("not applied") == 0);
}

TEST("an override with no home, on the root's pose or on a behavior is kept, said once and not applied") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    Scene scene;
    const EntityId root = Prefab::instantiate(scene, resources, LAMP, placedAt({1, 2, 3}));
    const uint32_t bulb = scene.get<PrefabEntity>(childOf(scene, root)).uid;
    scene.get<PrefabInstance>(root).overrides = {
        {PrefabEntity::ROOT, "Transform", "position", "[7, 7, 7]"},
        {bulb, "Script", "behaviors", "[]"},
        {bulb, "Camera", "fovY", "1.0"},
        {bulb, "Light", "brightness", "1.0"},
        {bulb, "Light", "intensity", "\"bright\""},
        {77, "Light", "intensity", "1.0"},
    };
    const std::string saved = SceneSerializer::saveToString(scene, resources);

    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(saved, loaded, resources));
    CHECK(loaded.get<Transform>(root).position == glm::vec3(1, 2, 3));
    CHECK(loaded.get<Light>(childOf(loaded, root)).intensity == 4.0f);
    CHECK(!loaded.has<Camera>(childOf(loaded, root)));
    CHECK(countInLog("root Transform") == 1);
    CHECK(countInLog("behavior fields") == 1);
    CHECK(countInLog("no such component") == 1);
    CHECK(countInLog("no such field") == 1);
    CHECK(countInLog("type does not match") == 1);
    CHECK(countInLog("no such entity") == 1);
    // Every one goes back as it came: restoring the prefab's home brings it to life.
    CHECK(loaded.get<PrefabInstance>(root).overrides.size() == 6u);
    CHECK(SceneSerializer::saveToString(loaded, resources) == saved);
}

TEST("a prefab file stores its parents by place, never as Hierarchy, and refuses a nested instance") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    json file = json::parse(readText(Roots::resolveProjectPath(LAMP)));
    REQUIRE(file["entities"].size() == 2u);
    CHECK(file["entities"][0]["uid"] == PrefabEntity::ROOT);
    // Its place plus one, as every reference in the file names an entity; zero is none.
    CHECK(file["entities"][1]["parent"] == 1);
    for (json entry : file["entities"]) CHECK(!entry["components"].contains("Hierarchy"));
    CHECK(file["entities"][1]["components"].contains("Transform"));

    // An instance inside the subtree would be flattened into the file: refused, nothing written.
    Scene scene;
    const EntityId holder = scene.createEntity();
    scene.add(holder, Transform{});
    const EntityId inner = Prefab::instantiate(scene, resources, LAMP, placedAt({0, 0, 0}));
    Ops::setParent(scene, inner, holder);
    CHECK(!Prefab::save(scene, holder, "prefabs/nested.json", resources));
    CHECK(!std::filesystem::exists(Roots::resolveProjectPath("prefabs/nested.json")));
    CHECK(!Prefab::save(scene, childOf(scene, inner), "prefabs/part.json", resources));
    CHECK(countInLog("nested prefabs are not supported") == 2);
}

TEST("a prefab whose entry names no earlier entry as its parent is refused whole") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    const json good = json::parse(readText(Roots::resolveProjectPath(LAMP)));
    // The bulb's parent as none, as itself and as an entry past the file's end.
    for (const int parent : {0, 2, 3}) {
        json bad = good;
        bad["entities"][1]["parent"] = parent;
        const std::string file = "prefabs/parent_" + std::to_string(parent) + ".json";
        writeText(Roots::resolveProjectPath(file), bad.dump());
        Scene scene;
        CHECK(!Prefab::instantiate(scene, resources, file));
        CHECK(scene.entityCount() == 0u);
    }
    CHECK(countInLog("does not name an earlier entry as its parent") == 3);
}

TEST("a key no field of this build reads is warned of once a read of the prefab, not once an instance") {
    openProject();
    ResourceManager resources;
    writeLamp(resources);
    json file = json::parse(readText(Roots::resolveProjectPath(LAMP)));
    file["entities"][1]["components"]["Light"]["brightness"] = 3;
    file["entities"][1]["components"]["Ocean"]               = json::object();
    writeText(Roots::resolveProjectPath(LAMP), file.dump());

    Scene scene;
    const EntityId first = Prefab::instantiate(scene, resources, LAMP, placedAt({0, 0, 0}));
    REQUIRE(first);
    REQUIRE(Prefab::instantiate(scene, resources, LAMP, placedAt({4, 0, 0})));
    CHECK(countInLog("Unknown field 'Light.brightness' in 'prefabs/lamp.json'") == 1);
    CHECK(countInLog("Unknown component key 'Ocean' in 'prefabs/lamp.json'") == 1);
    CHECK(scene.get<Light>(childOf(scene, first)).intensity == 4.0f);
}
