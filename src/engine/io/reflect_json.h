#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/reflect.h"
#include "io/json_vec.h"
#include "resource/resource_handle.h"

/**
 * @brief A reflected struct to JSON and back, field by field, by its VKM_REFLECT names.
 *
 * What the scene file is written with, and what a project keeps its own files with - its
 * settings, a save. Leaves, scoped enums by their VKM_ENUM_NAMES, nested reflected structs and
 * vectors of any of them go through; a field missing from the JSON keeps its value, so a file
 * written before a field was added still loads. The context decides the rest: a Handle needs one
 * that turns it into its asset's name and back (ComponentSerializer's), and a field holding the
 * wrong type of value is an error in a scene - the load reports it, naming where it stood - but
 * in a project's own file keeps its value and warns (OwnFile, which toJson and fromJson use). A
 * glm value of the wrong shape warns and keeps its own in either.
 */
namespace Vkm::Engine::ReflectJson {

// The leaves every field comes down to; a new field type adds a pair here.
inline nlohmann::json leafToJson(bool v) { return v; }
inline nlohmann::json leafToJson(int v) { return v; }
inline nlohmann::json leafToJson(uint32_t v) { return v; }
inline nlohmann::json leafToJson(float v) { return v; }
inline nlohmann::json leafToJson(const std::string& s) { return s; }
inline nlohmann::json leafToJson(const glm::vec2& v) { return detail::vec2ToJson(v); }
inline nlohmann::json leafToJson(const glm::vec3& v) { return detail::vec3ToJson(v); }
inline nlohmann::json leafToJson(const glm::vec4& v) { return detail::vec4ToJson(v); }
inline nlohmann::json leafToJson(const glm::quat& q) { return detail::quatToJson(q); }

inline void leafFromJson(const nlohmann::json& j, bool& v) { v = j.get<bool>(); }
inline void leafFromJson(const nlohmann::json& j, int& v) { v = j.get<int>(); }
inline void leafFromJson(const nlohmann::json& j, uint32_t& v) { v = j.get<uint32_t>(); }
inline void leafFromJson(const nlohmann::json& j, float& v) { v = j.get<float>(); }
inline void leafFromJson(const nlohmann::json& j, std::string& s) { s = j.get<std::string>(); }
inline void leafFromJson(const nlohmann::json& j, glm::vec2& v) { v = detail::jsonToVec2(j, v); }
inline void leafFromJson(const nlohmann::json& j, glm::vec3& v) { v = detail::jsonToVec3(j, v); }
inline void leafFromJson(const nlohmann::json& j, glm::vec4& v) { v = detail::jsonToVec4(j, v); }
inline void leafFromJson(const nlohmann::json& j, glm::quat& q) { q = detail::jsonToQuat(j, q); }

// Fixed-size character buffers (Name::value).
template<std::size_t N>
inline nlohmann::json leafToJson(const char (&v)[N]) { return std::string(v); }
template<std::size_t N>
inline void leafFromJson(const nlohmann::json& j, char (&v)[N]) {
    const std::string s = j.get<std::string>();
    std::strncpy(v, s.c_str(), N - 1);
    v[N - 1] = '\0';
}

// Scoped enums registered via VKM_ENUM_NAMES. The SFINAE leaves non-enum types to the overloads
// above.
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline nlohmann::json leafToJson(E v) { return Reflect::enumName(v); }
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline void leafFromJson(const nlohmann::json& j, E& v) {
    const std::string name = j.get<std::string>();
    // An unknown name keeps the constructed default and warns, rather than becoming a
    // valid-looking enumerator zero.
    if (!Reflect::enumFromNameChecked(name, v)) {
        LOG_WARNING_C(
            "IO",
            "No enumerator called '%s' in this build; leaving the field at its default",
            name.c_str()
        );
    }
}

template<typename T>             struct IsHandle                     : std::false_type {};
template<typename A>             struct IsHandle<Handle<A>>          : std::true_type  {};
template<typename T>             struct IsVector                     : std::false_type {};
template<typename E, typename A> struct IsVector<std::vector<E, A>>  : std::true_type  {};

/**
 * @brief The context for plain data in a scene, which names no assets: a number, string, list
 *        or struct of the wrong type throws, for the load to report.
 *
 * Reaching a Handle with it is a static_assert: such a struct needs a context that resolves
 * names (`handleToJson`, `handleFromJson`).
 */
struct PlainData {
    static constexpr bool KEEPS_WRONG_FIELDS = false;
};

/// Plain data in a project's own file: a field of the wrong type keeps its value and warns.
struct OwnFile : PlainData {
    static constexpr bool KEEPS_WRONG_FIELDS = true;
};

template<typename T, typename Ctx>
nlohmann::json saveFields(const T& obj, const Ctx& ctx);
template<typename T, typename Ctx>
void loadFields(const nlohmann::json& j, T& obj, const Ctx& ctx, bool inArray = false);

/**
 * @brief One value to JSON, whatever kind of value it is: a handle its name, a vector an array,
 *        a reflected struct an object, anything else a leaf.
 */
template<typename V, typename Ctx>
nlohmann::json valueToJson(const V& val, const Ctx& ctx) {
    if constexpr (IsHandle<V>::value) {
        static_assert(!std::is_base_of_v<PlainData, Ctx>, "a Handle<T> needs a context that names assets");
        return ctx.handleToJson(val);
    } else if constexpr (IsVector<V>::value) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& element : val) arr.push_back(valueToJson(element, ctx));
        return arr;
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        return saveFields(val, ctx);
    } else {
        return leafToJson(val);
    }
}

/**
 * @brief The same in the other direction.
 *
 * @param field Key the value is stored under, which a handle's context may keep an unresolved
 *              name under; null inside an array, where there is no key.
 */
template<typename V, typename Ctx>
void valueFromJson(const nlohmann::json& j, V& val, const Ctx& ctx, const char* field) {
    if constexpr (IsHandle<V>::value) {
        static_assert(!std::is_base_of_v<PlainData, Ctx>, "a Handle<T> needs a context that names assets");
        ctx.handleFromJson(j, val, field);
    } else if constexpr (IsVector<V>::value && Ctx::KEEPS_WRONG_FIELDS) {
        // Read aside, each element strictly, and kept whole: a list that is no list, or wrong
        // anywhere inside - a value of the wrong type, an element that is no object - keeps the one
        // there was rather than an element half its defaults. A glm value of the wrong shape, or
        // an enumerator this build does not know, reads as its default with a warning, as anywhere.
        V read;
        const nlohmann::json::array_t& elements = j.get_ref<const nlohmann::json::array_t&>();
        read.reserve(elements.size());
        for (const nlohmann::json& element : elements) {
            typename V::value_type item{};
            valueFromJson(element, item, PlainData{}, nullptr);
            read.push_back(std::move(item));
        }
        val = std::move(read);
    } else if constexpr (IsVector<V>::value) {
        // A list that is no list is a value of the wrong type, as any other.
        const nlohmann::json::array_t& elements = j.get_ref<const nlohmann::json::array_t&>();
        val.clear();
        val.reserve(elements.size());
        for (const nlohmann::json& element : elements) {
            typename V::value_type item{};
            valueFromJson(element, item, ctx, nullptr);
            val.push_back(std::move(item));
        }
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        // A struct inside an array stays inside it: its keys address the element.
        loadFields(j, val, ctx, field == nullptr);
    } else {
        leafFromJson(j, val);
    }
}

template<typename T, typename Ctx>
nlohmann::json saveFields(const T& obj, const Ctx& ctx) {
    nlohmann::json out = nlohmann::json::object();
    Reflect::forEachField(obj, [&](std::string_view name, const auto& val) {
        out[std::string(name)] = valueToJson(val, ctx);
    });
    return out;
}

template<typename T, typename Ctx>
void loadFields(const nlohmann::json& j, T& obj, const Ctx& ctx, bool inArray) {
    if (!j.is_object()) {
        // Wrong as any field of the wrong type is: kept in a project's own file, refused anywhere else.
        if constexpr (Ctx::KEEPS_WRONG_FIELDS) {
            LOG_WARNING_C("IO", "a struct holds no object; leaving it as it was");
        } else {
            (void)j.get_ref<const nlohmann::json::object_t&>();
        }
        return;
    }
    Reflect::forEachField(obj, [&](std::string_view name, auto& val) {
        const std::string key(name);
        const auto        it = j.find(key);
        if (it == j.end()) return;
        if constexpr (Ctx::KEEPS_WRONG_FIELDS) {
            try {
                valueFromJson(*it, val, ctx, inArray ? nullptr : key.c_str());
            } catch (const nlohmann::json::exception&) {
                LOG_WARNING_C(
                    "IO",
                    "'%s' holds a value of the wrong type; leaving the field as it was",
                    key.c_str()
                );
            }
        } else {
            valueFromJson(*it, val, ctx, inArray ? nullptr : key.c_str());
        }
    });
}

/**
 * @brief A reflected struct of plain data as a JSON object, a key a field.
 *
 * @param obj The struct.
 * @return Its fields.
 */
template<typename T>
nlohmann::json toJson(const T& obj) {
    return saveFields(obj, OwnFile{});
}

/**
 * @brief The fields @p j has written into @p obj; any it lacks, or holds wrongly, kept.
 *
 * @param j   An object, as toJson writes; anything else changes nothing.
 * @param obj The struct.
 */
template<typename T>
void fromJson(const nlohmann::json& j, T& obj) {
    loadFields(j, obj, OwnFile{});
}

} // namespace Vkm::Engine::ReflectJson
