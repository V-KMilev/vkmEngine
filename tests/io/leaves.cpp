// One rule for what a value in a file may be, read three ways: a scene's component (the load fails),
// a behavior's field (the value is kept, with a warning) and a project's own file (the same).

#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/json/reflect_json.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace LeafProbe {

struct Leaves {
    glm::vec3   v = {9.0f, 9.0f, 9.0f};
    float       f = 9.0f;
    int         i = 9;
    bool        b = true;
    std::string s = "kept";
    glm::quat   q = {1.0f, 0.0f, 0.0f, 0.0f};
};

class LeafReader : public Behavior<LeafReader> {
    public:
        glm::vec3   v = {9.0f, 9.0f, 9.0f};
        float       f = 9.0f;
        int         i = 9;
        bool        b = true;
        std::string s = "kept";
        glm::quat   q = {1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace LeafProbe

VKM_REFLECT_BEGIN(::LeafProbe::Leaves)
    VKM_F(v)
    VKM_F(f)
    VKM_F(i)
    VKM_F(b)
    VKM_F(s)
    VKM_F(q)
VKM_REFLECT_END()

VKM_BEHAVIOR(::LeafProbe::LeafReader)
    VKM_F(v)
    VKM_F(f)
    VKM_F(i)
    VKM_F(b)
    VKM_F(s)
    VKM_F(q)
VKM_REFLECT_END()

namespace {

using LeafProbe::LeafReader;
using LeafProbe::Leaves;

/// @p fields as a behavior a scene names, read by a scene load; the behavior's values after it.
Leaves readAsBehavior(const json& fields) {
    const json doc = {
        {"version", 1},
        {"entities", {{{"id", 1}, {"components", {{"Script", {{"behaviors", {{{"type", "LeafReader"}, {"properties", fields}}}}}}}}}}}
    };
    Scene scene;
    ResourceManager resources;
    REQUIRE(SceneSerializer::loadFromString(doc.dump(), scene, resources));
    const ScriptComponent& script = scene.get<ScriptComponent>(scene.entityAt(1));
    REQUIRE(script.behaviors.size() == 1u);
    const LeafReader* read = dynamic_cast<const LeafReader*>(script.behaviors[0].get());
    REQUIRE(read != nullptr);
    return Leaves{read->v, read->f, read->i, read->b, read->s, read->q};
}

bool sameLeaves(const Leaves& a, const Leaves& b) {
    return a.v == b.v && a.f == b.f && a.i == b.i && a.b == b.b && a.s == b.s && a.q == b.q;
}

} // namespace

TEST("a value of the wrong shape or type is refused alike by a component, a behavior and an own file") {
    const std::vector<std::pair<const char*, json>> wrong = {
        {"v", json(5)},
        {"v", json::array({1, 2})},
        {"v", json::array({1, "x", 3})},
        {"v", json()},
        {"v", json::array({1e300, 0, 0})},
        {"f", json(true)},
        {"f", json("1")},
        {"f", json(1e300)},
        {"i", json(2.5e10)},
        {"b", json(1)},
        {"s", json(3)},
        {"q", json::array({1, 0, 0})},
    };
    const Leaves untouched;
    for (const auto& [field, value] : wrong) {
        // Beside it, a sound value of another field, which must still be read.
        const json fields = {{field, value}, {std::string(
            field
        ) == "s" ? "i" : "s", std::string(field) == "s" ? json(4) : json("read")}};
        const std::string what = std::string(field) + " = " + value.dump();

        Leaves component;
        bool threw = false;
        try {
            ComponentSerializer::load(fields, component);
        } catch (const json::exception&) {
            threw = true;
        }
        CHECK(threw);
        if (!threw) std::printf("  a component took %s\n", what.c_str());

        const int warnedBefore = countInLog("keeping its current value");
        const Leaves behavior  = readAsBehavior(fields);
        CHECK(countInLog("keeping its current value") == warnedBefore + 1);
        CHECK((behavior.v == untouched.v || std::string(field) != "v"));
        CHECK((behavior.f == untouched.f || std::string(field) != "f"));
        CHECK(behavior.i == (std::string(field) == "s" ? 4 : untouched.i));
        CHECK(behavior.s == (std::string(field) == "s" ? untouched.s : "read"));

        const int ownBefore = countInLog("wrong type");
        Leaves own;
        ReflectJson::fromJson(fields, own);
        CHECK(countInLog("wrong type") == ownBefore + 1);
        CHECK((own.v == untouched.v || std::string(field) != "v"));
        CHECK((own.f == untouched.f || std::string(field) != "f"));
        CHECK(own.b == untouched.b);
        CHECK(own.q == untouched.q);
        CHECK(own.s == (std::string(field) == "s" ? untouched.s : "read"));
    }
}

TEST("a sound value reads the same in a component, a behavior and an own file, and writes the same") {
    const json fields = {
        {"v", json::array({1, 2, 3, 4})},
        {"f", 0.25},
        {"i", -7},
        {"b", false},
        {"s", "word"},
        {"q", json::array({0, 1, 0, 0})},
    };
    Leaves component;
    ComponentSerializer::load(fields, component);
    const Leaves behavior = readAsBehavior(fields);
    Leaves own;
    ReflectJson::fromJson(fields, own);

    // A vector takes the numbers it needs; a quaternion is written w first.
    CHECK(component.v == glm::vec3(1, 2, 3));
    CHECK(component.q == glm::quat(0, 1, 0, 0));
    CHECK(sameLeaves(component, behavior));
    CHECK(sameLeaves(component, own));

    // Written back, a behavior's fields and a reflected struct's are the same JSON.
    Scene scene;
    ResourceManager resources;
    const EntityId holder = scene.createEntity();
    LeafReader& written   = addBehavior<LeafReader>(scene, holder);
    written.v = behavior.v;
    written.f = behavior.f;
    written.i = behavior.i;
    written.b = behavior.b;
    written.s = behavior.s;
    written.q = behavior.q;
    json doc = json::parse(SceneSerializer::saveToString(scene, resources));
    const json properties = doc["entities"][0]["components"]["Script"]["behaviors"][0]["properties"];
    CHECK(properties == ComponentSerializer::save(component));
    CHECK(properties == ReflectJson::toJson(own));
}
