// A reflect block: what the inspector, the scene file and a project's own files read a struct by.

#include <string>
#include <string_view>
#include <vector>

#include "base/reflect/reflect.h"

#include "harness/check.h"

namespace ReflectProbe {

struct Lamp {
    float       brightness = 1.0f;
    int         bulbs      = 2;
    std::string label;
    bool        lit        = false;
};

} // namespace ReflectProbe

VKM_REFLECT_BEGIN(::ReflectProbe::Lamp)
    VKM_F(brightness, 0.0f, 10.0f, "How bright")
    VKM_F(bulbs, "How many")
    VKM_F(label)
VKM_REFLECT_END()

namespace Reflect = Vkm::Engine::Reflect;

TEST("a reflected struct lists its fields in order, by name, with their ranges and tips, and no others") {
    CHECK(std::string_view(Reflect::Traits<ReflectProbe::Lamp>::NAME) == "Lamp");
    CHECK(Reflect::IS_REFLECTED<ReflectProbe::Lamp>);
    CHECK(!Reflect::IS_REFLECTED<int>);

    ReflectProbe::Lamp lamp;
    std::vector<std::string> names;
    Reflect::forEachField(lamp, [&](std::string_view name, auto& value) {
        names.emplace_back(name);
        // Each field is the struct's own, written through.
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, float>) value = 4.5f;
        if constexpr (std::is_same_v<V, int>) value = 7;
        if constexpr (std::is_same_v<V, std::string>) value = "porch";
    });
    // `lit` has no row: it is not reflected, so nothing saves it.
    CHECK(names == std::vector<std::string>{"brightness", "bulbs", "label"});
    CHECK(lamp.brightness == 4.5f);
    CHECK(lamp.bulbs == 7);
    CHECK(lamp.label == "porch");

    constexpr auto fields = Reflect::Traits<ReflectProbe::Lamp>::fields();
    CHECK(std::get<0>(fields).min == 0.0f);
    CHECK(std::get<0>(fields).max == 10.0f);
    CHECK(std::string_view(std::get<0>(fields).tip) == "How bright");
    CHECK(std::get<1>(fields).min == std::get<1>(fields).max);
    CHECK(std::string_view(std::get<1>(fields).tip) == "How many");
    CHECK(std::get<2>(fields).tip == nullptr);
}
