// A behavior's fields: what its reflect block lists is saved, shown and copied, an asset it names goes
// in the scene's assets, and a behavior no module knows is kept as it was written.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/reflect/enum_names.h"
#include "ecs/component/scene/name.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"

namespace ScriptFields {

using namespace Vkm::Engine;

enum class Mood { Calm, Wary, Angry, Count };

/**
 * @brief A reflected struct inside a behavior.
 */
struct Reach {
    float     distance = 2.0f;
    glm::vec2 cone     = {0.5f, 1.0f};
};

/**
 * @brief One field of every kind a behavior may have, and one it keeps to itself.
 */
class TunedFields : public Behavior<TunedFields> {
    public:
        float       speed     = 3.0f;
        int         lives     = 2;
        bool        brave     = true;
        std::string title     = "knight";
        glm::vec3   glowColor = {1.0f, 0.5f, 0.0f};
        glm::vec4   tint      = {0.1f, 0.2f, 0.3f, 0.4f};
        glm::quat   facing    = {1.0f, 0.0f, 0.0f, 0.0f};
        Mood        mood      = Mood::Calm;
        Reach       reach;
        int         scratch   = 0;  ///< Runtime state: not in the block.
};

/**
 * @brief Names an asset of two kinds and a prefab.
 */
class AssetNamer : public Behavior<AssetNamer> {
    public:
        AssetRef<AudioClipAsset> thud;
        AssetRef<MeshAsset>      shape;
        PrefabRef                spawns;
};

/**
 * @brief Runs, to show a held behavior beside it does not stop it.
 */
class HeldBeside : public Behavior<HeldBeside> {
    public:
        void onFixedUpdate(float) override { ++ran; }

    public:
        int ran = 0;
};

} // namespace ScriptFields

VKM_ENUM_NAMES(::ScriptFields::Mood, "Calm", "Wary", "Angry")

VKM_REFLECT_BEGIN(::ScriptFields::Reach)
    VKM_F(distance)
    VKM_F(cone)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptFields::TunedFields)
    VKM_F(speed, 0.0f, 10.0f, "How fast it goes")
    VKM_F(lives)
    VKM_F(brave)
    VKM_F(title)
    VKM_F(glowColor, "Its glow")
    VKM_F(tint)
    VKM_F(facing)
    VKM_F(mood)
    VKM_F(reach)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptFields::AssetNamer)
    VKM_F(thud)
    VKM_F(shape)
    VKM_F(spawns)
VKM_REFLECT_END()

VKM_BEHAVIOR(::ScriptFields::HeldBeside)
    VKM_F(ran)
VKM_REFLECT_END()

using namespace Vkm::Test;
using namespace ScriptFields;

namespace {

/// Each field a visit meets, as the inspector would list it: "<name>[<min>..<max>](<tip>)", with
/// "{" and "}" around a struct's.
class Lister : public BehaviorFieldVisitor {
    public:
        void field(const FieldInfo& f, float&) override       { add(f); }
        void field(const FieldInfo& f, int&) override         { add(f); }
        void field(const FieldInfo& f, bool&) override        { add(f); }
        void field(const FieldInfo& f, glm::vec2&) override   { add(f); }
        void field(const FieldInfo& f, glm::vec3&) override   { add(f); }
        void field(const FieldInfo& f, glm::vec4&) override   { add(f); }
        void field(const FieldInfo& f, glm::quat&) override   { add(f); }
        void field(const FieldInfo& f, std::string&) override { add(f); }
        void enumField(const FieldInfo& f, int&, const char* const* names, std::size_t count) override {
            add(f);
            listed.back() += "<" + std::string(names[0]) + "/" + std::to_string(count) + ">";
        }
        void assetField(const FieldInfo& f, std::string&, AssetType) override { add(f); }
        void prefabField(const FieldInfo& f, std::string&) override { add(f); }
        bool beginStruct(const FieldInfo& f) override {
            add(f);
            listed.push_back("{");
            return true;
        }
        void endStruct() override { listed.push_back("}"); }

    public:
        std::vector<std::string> listed;

    private:
        void add(const FieldInfo& f) {
            std::string line = f.name;
            if (f.min != f.max) line += "[" + std::to_string(
                int(f.min)
            ) + ".." + std::to_string(int(f.max)) + "]";
            if (f.tip) line += "(" + std::string(f.tip) + ")";
            listed.push_back(line);
        }
};

/// The fields of @p script's first behavior, as a scene file writes them.
nlohmann::json propertiesOf(const ScriptComponent& script) {
    return ComponentSerializer::save(script)["behaviors"][0]["properties"];
}

} // namespace

TEST("the reflect block's fields are listed for the inspector in order, with their ranges and tips, and no others") {
    TunedFields tuned;
    Lister lister;
    tuned.visitFields(lister);

    const std::vector<std::string> expected = {
        "speed[0..10](How fast it goes)", "lives", "brave", "title", "glowColor(Its glow)", "tint", "facing",
        "mood<Calm/3>", "reach", "{", "distance", "cone", "}",
    };
    CHECK(lister.listed == expected);
    CHECK(std::string(tuned.typeName()) == "TunedFields");
}

TEST("a behavior's reflected fields are saved, read back and copied, and its own state is not") {
    ScriptComponent written;
    auto tuned = std::make_unique<TunedFields>();
    tuned->speed     = 7.5f;
    tuned->lives     = 9;
    tuned->brave     = false;
    tuned->title     = "squire";
    tuned->glowColor = {0.0f, 1.0f, 0.25f};
    tuned->tint      = {4.0f, 3.0f, 2.0f, 1.0f};
    tuned->facing    = glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    tuned->mood      = Mood::Angry;
    tuned->reach     = Reach{6.0f, {0.25f, 3.0f}};
    tuned->scratch   = 42;
    written.behaviors.push_back(std::move(tuned));

    const nlohmann::json saved = ComponentSerializer::save(written);
    CHECK(saved["behaviors"][0]["type"] == "TunedFields");
    CHECK(saved["behaviors"][0]["properties"]["mood"] == "Angry");
    CHECK(!saved["behaviors"][0]["properties"].contains("scratch"));

    ScriptComponent read;
    ComponentSerializer::load(saved, read);
    REQUIRE(read.behaviors.size() == 1);
    const auto& back = static_cast<const TunedFields&>(*read.behaviors[0]);
    const auto copy  = written.behaviors[0]->clone();
    for (const TunedFields* each : {&back, static_cast<const TunedFields*>(copy.get())}) {
        CHECK(each->speed == 7.5f);
        CHECK(each->lives == 9);
        CHECK(!each->brave);
        CHECK(each->title == "squire");
        CHECK(each->glowColor == glm::vec3(0.0f, 1.0f, 0.25f));
        CHECK(each->tint == glm::vec4(4.0f, 3.0f, 2.0f, 1.0f));
        CHECK_NEAR(each->facing, glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f)), 1e-6f);
        CHECK(each->mood == Mood::Angry);
        CHECK(each->reach.distance == 6.0f);
        CHECK(each->reach.cone == glm::vec2(0.25f, 3.0f));
        CHECK(each->scratch == 0);
    }
    CHECK(propertiesOf(read) == saved["behaviors"][0]["properties"]);
}

TEST("an asset a behavior names goes in the scene's assets by its kind, and a prefab it names stays a path") {
    Frame world;
    const EntityId knight = world.scene.createEntity();
    AssetNamer& armed     = addBehavior<AssetNamer>(world.scene, knight);
    armed.thud   = AssetRef<AudioClipAsset>{"sfx:thud"};
    armed.shape  = AssetRef<MeshAsset>{"mesh:rock"};
    armed.spawns = PrefabRef{"prefabs/boom.json"};

    const nlohmann::json doc = nlohmann::json::parse(
        SceneSerializer::saveToString(world.scene, world.resources)
    );
    const auto lists = [&](const char* section, const std::string& name) {
        for (const nlohmann::json& row : doc["assets"][section]) {
            if (row.value("name", "") == name) return true;
        }
        return false;
    };
    CHECK(lists("sounds", "sfx:thud"));
    CHECK(lists("meshes", "mesh:rock"));
    CHECK(doc["assets"].dump().find("boom") == std::string::npos);

    Frame loaded;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), loaded.scene, loaded.resources));
    const ScriptComponent* script = loaded.scene.tryGet<ScriptComponent>(
        loaded.scene.entityAt(knight.slot())
    );
    REQUIRE(script);
    REQUIRE(script->behaviors.size() == 1);
    const auto& back = static_cast<const AssetNamer&>(*script->behaviors[0]);
    CHECK(back.thud.name == "sfx:thud");
    CHECK(back.shape.name == "mesh:rock");
    CHECK(back.spawns.path == "prefabs/boom.json");
}

TEST("an asset only a behavior held as text names stays in the scene's assets, so the module back finds it") {
    Frame world;
    world.resources.add(AudioClipAsset{}, "sfx:held");
    world.resources.add(MeshAsset{}, "mesh:held");
    const EntityId knight = world.scene.createEntity();
    ScriptComponent script;
    ComponentSerializer::load(nlohmann::json::parse(R"({"behaviors": [
        {"type": "NobodysBehavior", "properties": {"thud": "sfx:held", "deep": {"shapes": ["mesh:held"]}, "name": "x"}}
    ]})"), script);
    REQUIRE(script.unknown.size() == 1);
    world.scene.add(knight, std::move(script));

    const nlohmann::json doc = nlohmann::json::parse(
        SceneSerializer::saveToString(world.scene, world.resources)
    );
    const auto lists = [&](const char* section, const std::string& name) {
        for (const nlohmann::json& row : doc["assets"][section]) {
            if (row.value("name", "") == name) return true;
        }
        return false;
    };
    CHECK(lists("sounds", "sfx:held"));
    CHECK(lists("meshes", "mesh:held"));
}

TEST("a behavior no module registered is held as text in its place, written back as it was read, and the rest run") {
    const nlohmann::json script = nlohmann::json::parse(R"({"behaviors": [
        {"type": "HeldBeside", "properties": {"ran": 0}},
        {"type": "NobodysBehavior", "properties": {"aim": [1, 2, 3], "name": "x", "deep": {"on": true}}},
        {"type": "HeldBeside", "properties": {"ran": 5}}
    ]})");
    Game game;
    const EntityId holder = game.scene().createEntity();
    ScriptComponent held;
    ComponentSerializer::load(script, held);
    CHECK(held.behaviors.size() == 2);
    CHECK(held.unknown.size() == 1);
    game.scene().add(holder, std::move(held));

    game.run(3);

    const nlohmann::json written = ComponentSerializer::save(game.scene().get<ScriptComponent>(holder));
    REQUIRE(written["behaviors"].size() == 3);
    CHECK(written["behaviors"][0]["properties"]["ran"] == 3);
    CHECK(written["behaviors"][1] == script["behaviors"][1]);
    CHECK(written["behaviors"][2]["properties"]["ran"] == 8);
}

TEST("a behavior's key no field of its type reads is warned of once a file, and one held as text is not") {
    Frame world;
    // Saved before a field was renamed and a struct's removed, on two entities: each named once.
    const std::string doc = R"({"version": 1, "entities": [
        {"id": 1, "components": {"Script": {"behaviors": [{"type": "TunedFields",
            "properties": {"speed": 2, "pace": 4, "reach": {"distance": 3, "width": 1}}}]}}},
        {"id": 2, "components": {"Script": {"behaviors": [
            {"type": "TunedFields", "properties": {"pace": 5}},
            {"type": "NobodysBehavior", "properties": {"pace": 6}}]}}}]})";
    REQUIRE(SceneSerializer::loadFromString(doc, world.scene, world.resources));
    CHECK(countInLog("Unknown field 'Script.TunedFields.pace'") == 1);
    CHECK(countInLog("Unknown field 'Script.TunedFields.reach.width'") == 1);
    CHECK(countInLog("Unknown field") == 2);
    CHECK(countInLog("No behavior registered for 'NobodysBehavior'") == 1);

    const ScriptComponent& read = world.scene.get<ScriptComponent>(world.scene.entityAt(1));
    REQUIRE(read.behaviors.size() == 1);
    CHECK(static_cast<const TunedFields&>(*read.behaviors[0]).speed == 2.0f);
    CHECK(static_cast<const TunedFields&>(*read.behaviors[0]).reach.distance == 3.0f);
}
