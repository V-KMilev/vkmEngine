// Scene files: what a save writes is what a load builds, and a file that is wrong costs only what is
// wrong in it.

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "debug/error/engine_error_log.h"
#include "ecs/component/scene/missing_assets.h"
#include "ecs/hierarchy_operations.h"
#include "io/asset/generate/material_generators.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_components.h"
#include "io/scene/scene_serializer.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace SceneProbe {

class Keeper : public Behavior<Keeper> {
    public:
        float       speed  = 2.0f;
        int         lives  = 3;
        bool        armed  = false;
        std::string tag    = "a";
        glm::vec3   home   = {1.0f, 2.0f, 3.0f};
        glm::quat   facing = {1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace SceneProbe

VKM_BEHAVIOR(::SceneProbe::Keeper)
    VKM_F(speed)
    VKM_F(lives)
    VKM_F(armed)
    VKM_F(tag)
    VKM_F(home)
    VKM_F(facing)
VKM_REFLECT_END()

namespace {

/**
 * @brief Moves every reflected value off its default, so a field a save or a load drops shows as a
 *        difference: each leaf, each element of a list (and one more), each nested struct.
 */
struct Nudge {
    int n = 0;

    void operator()(bool& v) { v = !v; }
    void operator()(float& v) { v += 0.01f * static_cast<float>(++n % 7 + 1); }
    void operator()(int& v) { v += 1 + ++n % 3; }
    void operator()(uint16_t& v) { v = static_cast<uint16_t>(v + 1 + ++n % 3); }
    void operator()(uint32_t& v) { v += 1 + ++n % 3; }
    void operator()(std::string& s) { s += "n" + std::to_string(++n); }
    void operator()(glm::quat& q) {
        q = glm::normalize(
            glm::angleAxis(0.1f * static_cast<float>(++n % 5 + 1), glm::normalize(glm::vec3(1, 2, 3))) * q
        );
    }
    void operator()(EntityId&) {}
    template<glm::length_t L>
    void operator()(glm::vec<L, float>& v) {
        for (glm::length_t i = 0; i < L; ++i) (*this)(v[i]);
    }
    template<size_t N>
    void operator()(char (&v)[N]) {
        std::snprintf(v, N, "name%d", ++n);
    }
    template<typename A>
    void operator()(Handle<A>&) {}
    template<typename T>
    void operator()(std::vector<T>& list) {
        list.emplace_back();
        for (T& item : list) (*this)(item);
    }
    template<typename T>
    void operator()(T& value) {
        if constexpr (std::is_enum_v<T>) {
            value = static_cast<T>((static_cast<int>(value) + 1) % static_cast<int>(T::Count));
        } else if constexpr (Reflect::IS_REFLECTED<T>) {
            Reflect::forEachField(value, [&](std::string_view, auto& field) { (*this)(field); });
        } else {
            static_assert(Reflect::DEPENDENT_FALSE<T>, "a field type the test does not move");
        }
    }
};

json parse(const std::string& text) { return json::parse(text); }

/// The entity of @p doc's entities saved at @p slot; null when there is none.
json entityAt(const json& doc, uint32_t slot) {
    for (const json& entry : doc.at("entities")) {
        if (entry.value("id", 0u) == slot) return entry;
    }
    return json();
}

/// Move @p component's reflected fields, when it has any.
template<typename T>
void nudgeIfReflected(Nudge& nudge, T& component) {
    if constexpr (Reflect::IS_REFLECTED<T>) nudge(component);
}

} // namespace

TEST("a saved scene loads back the same, every built-in component and a behavior included") {
    Scene scene;
    ResourceManager resources;
    const MeshHandle cube = addGeneratedMesh(resources, generateCube());
    const MaterialHandle paint = generateDefaultMaterial(resources);

    // One entity per row of the scene format's own list, every reflected field moved.
    Nudge nudge;
    std::vector<EntityId> rows;
#define VKM_TEST_ROW(Type, Key)                 \
    {                                           \
        const EntityId id = scene.createEntity(); \
        Type component{};                       \
        nudgeIfReflected(nudge, component);     \
        scene.add(id, std::move(component));    \
        rows.push_back(id);                     \
    }
    VKM_SCENE_VALUE_COMPONENTS(VKM_TEST_ROW, VKM_TEST_ROW, VKM_TEST_ROW)
#undef VKM_TEST_ROW
    nudge(scene.environment());
    nudge(scene.physics());

    // What a game authors, by name, so a field dropped from its reflect block shows.
    const EntityId lamp = scene.createEntity();
    Transform at;
    at.position = {1.5f, 2.0f, -3.25f};
    at.scale    = {2.0f, 1.0f, 0.5f};
    scene.add(lamp, at);
    Light light;
    light.intensity   = 7.5f;
    light.color       = {1.0f, 0.5f, 0.25f};
    light.castShadows = false;
    scene.add(lamp, light);
    Mesh shape;
    shape.mesh     = cube;
    shape.material = paint;
    scene.add(lamp, shape);
    Rigidbody body;
    body.mass = 12.0f;
    scene.add(lamp, body);
    auto& keeper = addBehavior<SceneProbe::Keeper>(scene, lamp);
    nudge(keeper);

    const EntityId post = scene.createEntity();
    scene.add(post, Transform{});
    Joint joint;
    joint.connected  = lamp;
    joint.holdTorque = 30.0f;
    scene.add(post, joint);
    HierarchyOperations::setParent(scene, post, lamp);

    const std::string saved = SceneSerializer::saveToString(scene, resources);
    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(saved, loaded, resources));
    CHECK(SceneSerializer::saveToString(loaded, resources) == saved);
    CHECK(countInLog("Unknown component key") == 0);
    CHECK(countInLog("Unknown field") == 0);

    CHECK(loaded.entityCount() == scene.entityCount());
    CHECK(loaded.get<Light>(lamp).intensity == 7.5f);
    CHECK(loaded.get<Light>(lamp).color == glm::vec3(1.0f, 0.5f, 0.25f));
    CHECK(!loaded.get<Light>(lamp).castShadows);
    CHECK(loaded.get<Transform>(lamp).position == at.position);
    CHECK(loaded.get<Transform>(lamp).scale == at.scale);
    CHECK(loaded.get<Rigidbody>(lamp).mass == 12.0f);
    CHECK(loaded.get<Mesh>(lamp).mesh == cube);
    CHECK(loaded.get<Mesh>(lamp).material == paint);
    CHECK(loaded.get<Joint>(post).connected == lamp);
    CHECK(loaded.get<Joint>(post).holdTorque == 30.0f);
    CHECK(loaded.get<Hierarchy>(post).parent == lamp);
    const ScriptComponent& script = loaded.get<ScriptComponent>(lamp);
    REQUIRE(script.behaviors.size() == 1u);
    const auto* read = dynamic_cast<const SceneProbe::Keeper*>(script.behaviors[0].get());
    REQUIRE(read != nullptr);
    CHECK(read->speed == keeper.speed);
    CHECK(read->tag == keeper.tag);
    CHECK(read->home == keeper.home);

    // The format's names: an asset by its name, an entity by its slot, a component by its key.
    const json doc = parse(saved);
    json lampEntry = entityAt(doc, lamp.slot());
    CHECK(lampEntry["components"]["Mesh"]["mesh"] == resources.get(cube).name());
    CHECK(lampEntry["components"].contains("Transform"));
    CHECK(lampEntry["components"].contains("Script"));
    CHECK(entityAt(doc, post.slot())["components"]["Hierarchy"]["parent"] == lamp.slot());
    CHECK(entityAt(doc, post.slot())["components"]["Joint"]["connected"] == lamp.slot());
}

TEST("a document read with its world's generations brings every entity back under the id it had") {
    Scene scene;
    ResourceManager resources;
    scene.destroyEntity(scene.createEntity());
    const EntityId lamp = scene.createEntity();
    const EntityId post = scene.createEntity();
    scene.add(lamp, Transform{});
    scene.add(post, Transform{});
    Joint joint;
    joint.connected = lamp;
    scene.add(post, joint);
    REQUIRE(lamp.generation() > 0);

    const std::string saved = SceneSerializer::saveToString(scene, resources);
    const std::vector<uint32_t> generations = scene.slotGenerations();
    REQUIRE(SceneSerializer::loadFromString(saved, scene, resources, &generations));
    CHECK(scene.isAlive(lamp));
    CHECK(scene.isAlive(post));
    CHECK(scene.get<Joint>(post).connected == lamp);
}

TEST("a failed load changes nothing in the live scene") {
    Scene scene;
    ResourceManager resources;
    const EntityId kept = scene.createEntity();
    Light light;
    light.intensity = 3.0f;
    scene.add(kept, light);
    const std::string before = SceneSerializer::saveToString(scene, resources);

    // Good up to its last entity, whose Light holds a string where a number goes.
    const std::string bad = R"({"version": 1, "entities": [
        {"id": 1, "components": {"Name": {"value": "first"}}},
        {"id": 2, "components": {"Light": {"intensity": "bright"}}}]})";
    CHECK(!SceneSerializer::loadFromString(bad, scene, resources));
    CHECK(!SceneSerializer::loadFromString("{ this is not json", scene, resources));
    const std::filesystem::path file = scratch() / "broken.json";
    writeText(file, bad);
    CHECK(!SceneSerializer::load(scene, resources, file.string()));
    CHECK(!SceneSerializer::load(scene, resources, (scratch() / "absent.json").string()));

    CHECK(SceneSerializer::saveToString(scene, resources) == before);
    CHECK(scene.isAlive(kept));
}

TEST("an assets entry that is no object, or names no string, costs only itself") {
    Scene scene;
    ResourceManager resources;
    const std::string doc = R"({"version": 1, "assets": {"meshes": [42, {"name": 5}, "cube"]},
        "entities": [{"id": 4, "components": {"Name": {"value": "kept"}}}]})";
    REQUIRE(SceneSerializer::loadFromString(doc, scene, resources));
    CHECK(scene.isAliveAtIndex(4));
    CHECK(countInLog("mesh entry missing 'name'") == 3);
}

TEST("a key no field of this build reads is warned of once a file, and the rest loads") {
    Scene scene;
    ResourceManager resources;
    // Saved before a field was renamed, one removed, a list element's and a settings block's: each
    // named once however often it stands.
    const std::string doc = R"({"version": 1, "entities": [
        {"id": 1, "components": {"ParticleEmitter": {"rate": 0, "ratePerYard": 7}}},
        {"id": 2, "components": {"ParticleEmitter": {"ratePerYard": 3}, "UIButton": {"eventId": "go"}}},
        {"id": 3, "components": {"LOD": {"levels": [{"maxDistance": 5, "fade": 1}, {"fade": 2}]}}}],
        "environment": {"fog": {"density": 0.1, "thickness": 2}}})";
    REQUIRE(SceneSerializer::loadFromString(doc, scene, resources));
    CHECK(countInLog("Unknown field 'ParticleEmitter.ratePerYard'") == 1);
    CHECK(countInLog("Unknown field 'UIButton.eventId'") == 1);
    CHECK(countInLog("Unknown field 'LOD.levels.fade'") == 1);
    CHECK(countInLog("Unknown field 'environment.fog.thickness'") == 1);
    CHECK(countInLog("Unknown field") == 4);
    CHECK(scene.get<LOD>(scene.entityAt(3)).levels[0].maxDistance == 5.0f);
    CHECK(scene.environment().fog.density == 0.1f);
}

TEST("a value of the wrong type fails the load, naming the entity, the component and what it wanted") {
    Scene scene;
    ResourceManager resources;
    const char* cases[][2] = {
        {R"({"Light": {"intensity": "bright"}})", "number"},
        {R"({"Light": {"intensity": true}})", "number"},
        {R"({"Transform": {"position": [1, 2]}})", "3 numbers"},
        // Says what it found, not what it wanted: reported, not held.
        {R"({"Transform": {"position": 5}})", ""},
        {R"({"Camera": {"active": 1}})", "boolean"},
        {R"({"Light": {"intensity": 1e300}})", "range"},
        {R"({"Animation": {"length": 1e300}})", "range"},
        {R"({"Animation": {"scale": {"keyframes": [{"t": 1e300, "v": [1, 1, 1]}]}}})", "range"},
        // A field the reflection driver does not read: an asset's name, a ragdoll's bones, a track.
        {R"({"Mesh": {"mesh": 5}})", "string"},
        {R"({"Ragdoll": {"bones": 5}})", ""},
        {R"({"Ragdoll": {"bones": [5]}})", ""},
        {R"({"Ragdoll": {"bones": [{"bone": 0, "body": 0, "bodyFromBone": [1, 0, 0]}]}})", ""},
        {R"({"Animation": {"position": 5}})", ""},
        {R"({"Animation": {"position": {"keyframes": 5}}})", ""},
    };
    for (const auto& [components, wanted] : cases) {
        const std::string doc = std::string(R"({"version": 1, "entities": [{"id": 7, "components": )") + components + "}]}";
        CHECK(!SceneSerializer::loadFromString(doc, scene, resources));
        const std::string log = logText();
        const size_t line     = log.rfind("Aborted while reading");
        REQUIRE(line != std::string::npos);
        const std::string said = log.substr(line, log.find('\n', line) - line);
        const std::string key  = json::parse(components).begin().key();
        CHECK(said.find("entity 7") != std::string::npos);
        CHECK(said.find("component '" + key + "'") != std::string::npos);
        CHECK(said.find(wanted) != std::string::npos);
    }
    // An animation key whose value is the wrong shape is drift too.
    const std::string keyed = R"({"version": 1, "entities": [{"id": 3, "components": {"Animation":
        {"position": {"keyframes": [{"t": 0, "v": [1, "x", 3]}]}}}}]})";
    CHECK(!SceneSerializer::loadFromString(keyed, scene, resources));
    CHECK(countInLog("component 'Animation'") == 5);
}

TEST("an asset name that does not resolve is reported, kept, and written back on the next save") {
    // Named, not generated: a generator's mesh is rebuilt from its name wherever it is opened.
    ResourceManager authored;
    const MeshHandle level0 = authored.add(generateCube(), "test:far-mesh");
    Scene scene;
    const EntityId far = scene.createEntity();
    Mesh shape;
    shape.mesh = level0;
    scene.add(far, shape);
    LOD lod;
    lod.levels = {{level0, 10.0f}, {level0, 20.0f}, {level0, 40.0f}};
    scene.add(far, lod);
    const std::string name  = authored.get(level0).name();
    const std::string saved = SceneSerializer::saveToString(scene, authored);

    // Opened where that mesh is not loaded: the load stands, the slots empty, each miss on record.
    ResourceManager without;
    EngineErrorLog errors;
    setErrorSink(&errors);
    Scene opened;
    REQUIRE(SceneSerializer::loadFromString(saved, opened, without));
    setErrorSink(nullptr);
    CHECK(!opened.get<Mesh>(far).mesh);
    CHECK(errors.entries().size() == 1u);
    CHECK(errors.entries()[0].repeatCount == 4u);
    const std::vector<MissingAssetRef> missing = SceneSerializer::unresolvedRefs(opened, without, far);
    REQUIRE(missing.size() == 4u);
    CHECK(missing[0].component == "Mesh");
    CHECK(missing[0].field == "mesh");
    CHECK(missing[3].component == "LOD");
    CHECK(missing[3].field == "levels.2.mesh");
    CHECK(missing[3].name == name);

    // Saved again, each name is back in its field and in the assets block.
    json again = parse(SceneSerializer::saveToString(opened, without));
    json entry = entityAt(again, far.slot());
    CHECK(entry["components"]["Mesh"]["mesh"] == name);
    CHECK(entry["components"]["LOD"]["levels"][2]["mesh"] == name);
    CHECK(again["assets"].dump().find(name) != std::string::npos);

    // A field filled since keeps what it holds now, and no longer counts as missing.
    const MeshHandle other = without.add(generatePyramid(), "test:near-mesh");
    opened.get<Mesh>(far).mesh = other;
    CHECK(SceneSerializer::unresolvedRefs(opened, without, far).size() == 3u);
    const json filled = parse(SceneSerializer::saveToString(opened, without));
    CHECK(entityAt(filled, far.slot())["components"]["Mesh"]["mesh"] == without.get(other).name());
}

TEST("names a load could not resolve stay with that load: not a later one, not one on another thread") {
    ResourceManager authored;
    const MeshHandle lost = authored.add(generateCube(), "test:lost-mesh");
    Scene scene;
    const EntityId a = scene.createEntity();
    Mesh shape;
    shape.mesh = lost;
    scene.add(a, shape);
    const std::string missingMesh = SceneSerializer::saveToString(scene, authored);
    const std::string name        = authored.get(lost).name();

    // A load that misses a name and then fails on a later field of the same component.
    json failing = parse(missingMesh);
    failing["entities"][0]["components"]["Mesh"]["visible"] = "bright";
    ResourceManager empty;
    Scene into;
    CHECK(!SceneSerializer::loadFromString(failing.dump(), into, empty));

    // The next load's record holds its own misses only.
    const std::string clean = R"({"version": 1, "entities": [{"id": 4, "components": {"Name": {"value": "x"}}}]})";
    REQUIRE(SceneSerializer::loadFromString(clean, into, empty));
    int records = 0;
    into.forEach<MissingAssets>([&](EntityId, MissingAssets&) { ++records; });
    CHECK(records == 0);

    // Two loads at once, each of a scene naming a mesh of its own, each keeps its own name.
    json other = parse(missingMesh);
    other["entities"][0]["components"]["Mesh"]["mesh"] = "elsewhere:mesh";
    std::vector<MissingAssetRef> left, right;
    for (int round = 0; round < 20; ++round) {
        std::thread worker([&] {
            ResourceManager mine;
            Scene built;
            if (SceneSerializer::loadFromString(
                other.dump(),
                built,
                mine
            )) left = SceneSerializer::unresolvedRefs(built, mine, a);
        });
        ResourceManager mine;
        Scene built;
        if (SceneSerializer::loadFromString(
            missingMesh,
            built,
            mine
        )) right = SceneSerializer::unresolvedRefs(built, mine, a);
        worker.join();
        REQUIRE(left.size() == 1u);
        REQUIRE(right.size() == 1u);
        CHECK(left[0].name == "elsewhere:mesh");
        CHECK(right[0].name == name);
    }
}

TEST("a joint's warm start and a body's session state are never saved") {
    Game game;
    Scene& scene = game.scene();
    const EntityId hook = scene.createEntity();
    Transform up;
    up.position = {0.0f, 5.0f, 0.0f};
    scene.add(hook, up);
    const EntityId weight = scene.createEntity();
    Transform hung;
    hung.position = {0.0f, 4.0f, 0.0f};
    scene.add(weight, hung);
    scene.add(weight, Rigidbody{});
    Collider collider;
    collider.parts = {ColliderPart{}};
    scene.add(weight, collider);
    Joint joint;
    joint.connected       = hook;
    joint.anchor          = {0.0f, 0.5f, 0.0f};
    joint.connectedAnchor = {0.0f, -0.5f, 0.0f};
    joint.holdTorque      = 5.0f;
    scene.add(weight, joint);

    game.run(30);
    // Hanging, the joint carries the weight: it holds an impulse, and an angle it keeps.
    REQUIRE(glm::length(scene.get<Joint>(weight).impulse) > 0.0f);

    const json doc = parse(SceneSerializer::saveToString(scene, game.resources()));
    json saved     = entityAt(doc, weight.slot())["components"];
    for (const char* runtime : {"impulse", "holdImpulse", "heldRotation", "heldResolved", "resolvedDistance"}) {
        CHECK(!saved["Joint"].contains(runtime));
    }
    CHECK(saved["Joint"]["holdTorque"] == 5.0f);
    CHECK(!saved["Rigidbody"].contains("sleeping"));

    Scene loaded;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), loaded, game.resources()));
    CHECK(loaded.get<Joint>(weight).impulse == glm::vec3(0.0f));
    CHECK(!loaded.get<Joint>(weight).heldResolved);
}
