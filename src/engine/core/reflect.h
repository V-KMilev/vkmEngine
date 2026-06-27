#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Engine::Reflect {

/**
 * @brief One (name, pointer-to-member) pair, the unit of compile-time
 *        field reflection.
 *
 * Templated on the owning type T and the member type M so generic
 * iteration (forEachField) can deduce the field's static type and
 * pick the right toJson / fromJson / inspector widget.
 */
template<typename T, typename M>
struct Field {
    std::string_view name;
    M T::*           ptr;
};

// CTAD: `Field{"position", &Transform::position}` deduces T=Transform, M=glm::vec3.
template<typename T, typename M>
Field(const char*, M T::*) -> Field<T, M>;

/**
 * @brief Primary trait. Specialise per component via VKM_REFLECT_BEGIN /
 *        VKM_F / VKM_REFLECT_END (below).
 *
 * Each specialisation must expose:
 *   static constexpr auto fields();   // -> std::tuple<Field<T, ...>...>
 *
 * Unspecialised use of Traits<T> is a compile error - which is the right
 * failure mode: a generic save/load helper called on a non-reflected
 * type tells you to add the reflection markup.
 */
template<typename T>
struct Traits;

/**
 * @brief Visit every reflected field of @p obj.
 *
 * Calls `fn(name, fieldRef)` for each field. @p obj may be const; the
 * field reference is then const too, picking up the read-only path in
 * overloaded helpers like toJson(const T&).
 */
template<typename T, typename Fn>
constexpr void forEachField(T& obj, Fn&& fn) {
    using Bare = std::remove_const_t<T>;
    auto tup = Traits<Bare>::fields();
    std::apply([&](auto&&... f) {
        ((fn(f.name, obj.*(f.ptr))), ...);
    }, tup);
}

/**
 * @brief Maps an enum to its value-ordered names. Specialise via
 *        VKM_ENUM_NAMES (below), which exposes:
 *
 *          static constexpr const char* const values[];  // index == enum value
 *          static constexpr std::size_t      count;       // == sizeof(values)
 *
 * The unspecialised primary is intentionally incomplete: naming an
 * unregistered enum is then a clear compile error rather than a silent
 * fallback. enumName / enumFromName / the editor's drawEnumCombo all read
 * this one table, so an enum's serialized names and its UI combo cannot drift.
 */
template<typename Enum>
struct EnumNames;

/**
 * @brief Enum value -> its serialized / display name.
 *
 * An out-of-range value falls back to the first name.
 */
template<typename Enum>
constexpr const char* enumName(Enum value) {
    using Names = EnumNames<Enum>;
    const auto index = static_cast<std::size_t>(value);
    return index < Names::count ? Names::values[index] : Names::values[0];
}

/**
 * @brief Parse an enum from a name, falling back to value 0 when unknown.
 */
template<typename Enum>
Enum enumFromName(std::string_view name) {
    using Names = EnumNames<Enum>;
    for (std::size_t i = 0; i < Names::count; ++i) {
        if (name == Names::values[i]) return static_cast<Enum>(i);
    }
    return static_cast<Enum>(0);
}

} // namespace Engine::Reflect

/**
 * @brief Macro shorthand for declaring a Traits specialisation.
 *
 * Invoke INSIDE namespace Engine, right after the type, with the unqualified
 * name (mirrors VKM_ENUM_NAMES):
 *
 *   namespace Engine {
 *   struct Transform { ... };
 *   VKM_REFLECT_BEGIN(Transform)
 *       VKM_F(position),
 *       VKM_F(rotation),
 *       VKM_F(scale)
 *   VKM_REFLECT_END()
 *   }
 *
 * It opens `namespace Reflect` (which, nested in Engine, is Engine::Reflect) and
 * specialises Traits there - no global-scope ::Engine:: dance, since the only
 * shadowed name is the class Engine, which this never spells. VKM_F() looks up
 * the `vkm_reflect_self` alias the macro injects so each field needn't repeat
 * the type name.
 *
 * Fields omitted from the macro are NOT serialised - that's the mechanism for
 * "internal only" data.
 */
#define VKM_REFLECT_BEGIN(Type)                                              \
    namespace Reflect {                                                      \
    template<> struct Traits<Type> {                                         \
        using vkm_reflect_self = Type;                                       \
        static constexpr auto fields() {                                     \
            return std::make_tuple(

#define VKM_F(name) \
    ::Engine::Reflect::Field{#name, &vkm_reflect_self::name}

#define VKM_REFLECT_END()                                                    \
            );                                                               \
        }                                                                    \
    };                                                                       \
    }

/**
 * @brief Register an enum's value-ordered names in one place.
 *
 * Invoke INSIDE namespace Engine, right after the enum, with the unqualified
 * type and the names in value order:
 *
 *   namespace Engine {
 *   enum class LightType { Directional, Point, ... , Count };
 *   VKM_ENUM_NAMES(LightType, "Directional", "Point", ...)
 *   }
 *
 * It opens `namespace Reflect` (which, nested in Engine, is Engine::Reflect) and
 * specialises EnumNames there - no global-scope ::Engine:: qualification needed,
 * since the only shadowed name is the class Engine, which this never spells.
 *
 * This one table is what enumName / enumFromName / drawEnumCombo read, so an
 * enum's serialized names and its editor combo cannot drift. The enum must end
 * in a trailing `Count` sentinel: the static_assert ties the list length to it,
 * so adding a value without a name fails to compile.
 */
#define VKM_ENUM_NAMES(EnumType, ...)                                            \
    namespace Reflect {                                                          \
    template<> struct EnumNames<EnumType> {                                      \
        static constexpr const char* const values[] = { __VA_ARGS__ };           \
        static constexpr std::size_t count = sizeof(values) / sizeof(values[0]); \
        static_assert(count == static_cast<std::size_t>(EnumType::Count),        \
                      #EnumType " names out of sync with its Count sentinel");   \
    };                                                                           \
    }
