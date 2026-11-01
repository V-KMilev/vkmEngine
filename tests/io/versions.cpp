// What a scene or prefab file must be to load: a version this build reads, and a body that reads
// whole. Anything else is refused, never half taken, never a crash.

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "base/math/random.h"
#include "ecs/component/light/light.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/hierarchy_operations.h"
#include "io/project/roots.h"
#include "io/scene/prefab.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace {

/// The version this build writes for a scene, read off one it saved.
int newestScene() {
    Scene empty;
    ResourceManager resources;
    return json::parse(SceneSerializer::saveToString(empty, resources))["version"].get<int>();
}

/// The version this build writes for a prefab, read off one it saved.
int newestPrefab(ResourceManager& resources) {
    Scene scene;
    const EntityId root = scene.createEntity();
    scene.add(root, Transform{});
    REQUIRE(Prefab::save(scene, root, "prefabs/probe.json", resources));
    return json::parse(readText(Roots::resolveProjectPath("prefabs/probe.json")))["version"].get<int>();
}

} // namespace

TEST("a scene or prefab with no sound version, or one newer than this build, is refused, and every older one loads") {
    openProject();
    ResourceManager resources;
    const int scenes  = newestScene();
    const int prefabs = newestPrefab(resources);
    REQUIRE(scenes >= 1);
    REQUIRE(prefabs >= 1);

    const json unsound[] = {json(), json(0), json(-1), json("2"), json(2.5), json(1e300)};
    int at = 0;
    const auto refused = [&](const json& version, bool newer) {
        ++at;
        json scene  = {{"entities", json::array()}};
        json prefab = {{"entities", json::array({{{"uid", 0}, {"components", json::object()}}})}};
        if (newer) {
            scene["version"]  = scenes + 1;
            prefab["version"] = prefabs + 1;
        } else if (!version.is_null()) {
            scene["version"] = prefab["version"] = version;
        }

        const std::string sceneFile  = "scenes/refused_" + std::to_string(at) + ".json";
        const std::string prefabFile = "prefabs/refused_" + std::to_string(at) + ".json";
        writeText(Roots::resolveProjectPath(sceneFile), scene.dump());
        writeText(Roots::resolveProjectPath(prefabFile), prefab.dump());
        Scene live;
        CHECK(!SceneSerializer::load(live, resources, Roots::resolveProjectPath(sceneFile).string()));
        CHECK(!Prefab::instantiate(live, resources, prefabFile));
        // Each says so once, naming its file.
        CHECK(countInLog("refused_" + std::to_string(at) + ".json") == 2);
        CHECK(live.entityCount() == 0u);
    };
    for (const json& version : unsound) refused(version, false);
    refused(json(), true);

    for (int version = 1; version <= std::max(scenes, prefabs); ++version) {
        Scene live;
        if (version <= scenes) {
            const json doc = {{"version", version}, {"entities", json::array({{{"id", 3}}})}};
            CHECK(SceneSerializer::loadFromString(doc.dump(), live, resources));
            CHECK(live.isAliveAtIndex(3));
        }
        if (version <= prefabs) {
            const std::string file = "prefabs/v" + std::to_string(version) + ".json";
            const json doc = {{"version", version}, {"entities", json::array(
                {{{"uid", 0}, {"components", json::object()}}}
            )}};
            writeText(Roots::resolveProjectPath(file), doc.dump());
            CHECK(Prefab::instantiate(live, resources, file));
        }
    }
}

TEST("a damaged scene or prefab file is refused or loads whole, and a refused one leaves the live scene be") {
    openProject();
    ResourceManager resources;

    // A scene with a bit of everything a file holds: components, a parent, a joint, an instance.
    Scene source;
    const EntityId root = source.createEntity();
    source.add(root, Transform{});
    Light light;
    light.intensity = 3.0f;
    source.add(root, light);
    const EntityId child = source.createEntity();
    source.add(child, Transform{});
    Joint joint;
    joint.connected = root;
    source.add(child, joint);
    Vkm::Engine::HierarchyOperations::setParent(source, child, root);
    // Saved as a prefab, the root becomes an instance of it, so the scene holds a reference too.
    REQUIRE(Prefab::save(source, root, "prefabs/whole.json", resources));
    const std::string sceneText  = SceneSerializer::saveToString(source, resources);
    const std::string prefabText = readText(Roots::resolveProjectPath("prefabs/whole.json"));

    Scene live;
    const EntityId mark = live.createEntity();
    live.add(mark, Light{});
    const std::string before = SceneSerializer::saveToString(live, resources);

    Math::Rng rng(2026u);
    const char junk[] = "{}[]\",:0-9e.\0 tfn";
    int loaded = 0, refused = 0, built = 0;
    for (int round = 0; round < 400; ++round) {
        // Cut short, or a byte changed: whatever reads must read whole.
        std::string damaged = round % 2 == 0 ? sceneText : prefabText;
        if (round % 4 < 2) {
            damaged.resize(rng.nextInt(0, static_cast<int>(damaged.size()) - 1));
        } else {
            for (int hits = rng.nextInt(1, 4); hits > 0; --hits) {
                damaged[rng.nextInt(
                    0,
                    static_cast<int>(damaged.size()) - 1
                )] = junk[rng.nextInt(0, sizeof(junk) - 2)];
            }
        }
        if (round % 2 == 0) {
            if (SceneSerializer::loadFromString(damaged, live, resources)) {
                ++loaded;
                // It took: start the next round from the marked scene again.
                REQUIRE(SceneSerializer::loadFromString(before, live, resources));
            } else {
                ++refused;
                CHECK(SceneSerializer::saveToString(live, resources) == before);
            }
        } else {
            const std::string file = "prefabs/damaged_" + std::to_string(round) + ".json";
            writeText(Roots::resolveProjectPath(file), damaged);
            Scene into;
            built += static_cast<bool>(Prefab::instantiate(into, resources, file));
        }
    }
    CHECK(refused > 0);
    // Damage that parses and fails on the last entity, the others already read: still none of it is taken.
    json late = json::parse(sceneText);
    late["entities"].back()["components"]["Light"] = {{"intensity", "bright"}};
    CHECK(!SceneSerializer::loadFromString(late.dump(), live, resources));
    CHECK(SceneSerializer::saveToString(live, resources) == before);
    std::printf(
        "damaged scenes: %d loaded, %d refused; damaged prefabs built: %d of 200\n",
        loaded,
        refused,
        built
    );
}

TEST("entity records a file cannot hold are skipped and said, and the rest of the scene loads") {
    ResourceManager resources;
    // Damage that parses: ids past what a file may claim, repeated, or naming no parent.
    const std::string odd = R"({"version": 1, "entities": [
        {"id": 4000000000, "components": {"Light": {}}},
        {"id": 0, "components": {"Light": {}}},
        {"id": 5, "components": {"Hierarchy": {"parent": 8}}},
        {"id": 5, "components": {"Light": {}}},
        {"id": 6, "components": {"Hierarchy": {"parent": 99}, "Joint": {"connected": 77}}},
        {"id": 8, "components": {"Hierarchy": {"parent": 5}}}]})";
    Scene strange;
    REQUIRE(SceneSerializer::loadFromString(odd, strange, resources));
    CHECK(strange.entityCount() == 3u);
    CHECK(strange.isAliveAtIndex(5));
    CHECK(strange.isAliveAtIndex(6));
    CHECK(!strange.get<Joint>(strange.entityAt(6)).connected);
    CHECK(countInLog("id 0 is the reserved slot") == 1);
    CHECK(countInLog("already taken") == 1);
    CHECK(countInLog("slot 77") == 1);
    CHECK(countInLog("Parent slot 99") == 1);
    // Two that name each other as parent are no ring: the second link is refused.
    CHECK(countInLog("cycle") == 1);

    // A prefab's entity named by a number no uid can hold is refused, the file whole.
    openProject();
    writeText(Roots::resolveProjectPath("prefabs/huge_uid.json"), R"({"version": 1, "entities": [
        {"uid": 0, "components": {}}, {"uid": 1e300, "parent": 0, "components": {}}]})");
    Scene into;
    CHECK(!Prefab::instantiate(into, resources, "prefabs/huge_uid.json"));
    CHECK(countInLog("carries no uid") == 1);
}
