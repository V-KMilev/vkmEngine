#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Vkm::Engine::Reflect {

/**
 * @brief False, but only once T is known: the condition of the static_assert that ends an
 *        `if constexpr` chain.
 *
 * @tparam T The type the chain dispatched on.
 */
template<typename T>
inline constexpr bool DEPENDENT_FALSE = false;

/**
 * @brief One (name, pointer-to-member) pair, the unit of compile-time field reflection.
 *
 * Templated on owner and member type so forEachField can deduce the field's type.
 */
template<typename T, typename M>
struct Field {
    std::string_view name;
    M T::*           ptr;
};

// `Field{"position", &Transform::position}` deduces T=Transform, M=glm::vec3.
template<typename T, typename M>
Field(const char*, M T::*) -> Field<T, M>;

/**
 * @brief The reflection trait, specialised per type by VKM_REFLECT_BEGIN / VKM_F / VKM_REFLECT_END.
 *
 * Exposes NAME (no namespace) and fields() -> std::tuple<Field<T, ...>...>.
 * Unspecialised use is a compile error.
 */
template<typename T>
struct Traits;

/**
 * @brief Visit every reflected field of @p obj.
 *
 * @tparam T  A type with a Traits specialisation; const gives const field refs.
 * @tparam Fn Callable taking (std::string_view, field reference).
 * @param obj Object whose fields are visited.
 * @param fn  Called once per reflected field, in Traits order.
 */
template<typename T, typename Fn>
constexpr void forEachField(T& obj, Fn&& fn) {
    using Bare = std::remove_const_t<T>;
    constexpr auto tup = Traits<Bare>::fields();
    const auto visit = [&](auto&&... f) { ((fn(f.name, obj.*(f.ptr))), ...); };
    std::apply(visit, tup);
}

/**
 * @brief The last `::` segment of a qualified type name.
 *
 * What VKM_REFLECT_BEGIN records as NAME: `::Game::Spinner` is "Spinner".
 *
 * @param qualified A type name as written, namespace and all.
 * @return A pointer into @p qualified past the last `::`, or all of it.
 */
constexpr const char* shortName(const char* qualified) {
    const char* last = qualified;
    for (const char* p = qualified; *p; ++p) {
        if (p[0] == ':' && p[1] == ':') last = p + 2;
    }
    return last;
}

/**
 * @brief Maps an enum to its value-ordered names.
 *
 * Specialised by VKM_ENUM_NAMES, exposing `values[]` (indexed by enum value)
 * and `count`. An unregistered enum is a compile error.
 */
template<typename Enum>
struct EnumNames;

/**
 * @brief Enum value -> its serialized / display name.
 *
 * @tparam Enum An enum registered with VKM_ENUM_NAMES.
 * @param value Enumerator to name.
 * @return Its name; the first name when out of range.
 */
template<typename Enum>
constexpr const char* enumName(Enum value) {
    using Names = EnumNames<Enum>;
    const auto index = static_cast<std::size_t>(value);
    return index < Names::count ? Names::values[index] : Names::values[0];
}

/**
 * @brief Parse an enum from a name, saying whether the name was one.
 *
 * @tparam Enum An enum registered with VKM_ENUM_NAMES.
 * @param name  Serialized name to look up.
 * @param[out] out Set to the match; untouched on a miss, keeping the caller's default.
 * @return False when this build has no enumerator by that name.
 */
template<typename Enum>
bool enumFromNameChecked(std::string_view name, Enum& out) {
    using Names = EnumNames<Enum>;
    for (std::size_t i = 0; i < Names::count; ++i) {
        if (name == Names::values[i]) {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    return false;
}

/**
 * @brief True iff T has a Traits specialisation.
 *
 * Lets a generic walker tell a nested reflected struct from a leaf.
 */
template<typename T, typename = void>
inline constexpr bool IS_REFLECTED = false;

template<typename T>
inline constexpr bool IS_REFLECTED<T, std::void_t<decltype(Traits<T>::fields())>> = true;

/**
 * @brief True iff Enum has a VKM_ENUM_NAMES registration.
 */
template<typename Enum, typename = void>
inline constexpr bool HAS_ENUM_NAMES = false;

template<typename Enum>
inline constexpr bool HAS_ENUM_NAMES<Enum, std::void_t<decltype(EnumNames<Enum>::count)>> = true;

} // namespace Vkm::Engine::Reflect

/**
 * @brief Macro shorthand for declaring a Traits specialisation.
 *
 * Invoke at global scope, after the type's namespace has closed, naming the type
 * in full; inside a namespace the compiler says "'Traits' is not a class template".
 *
 *   VKM_REFLECT_BEGIN(::Vkm::Engine::Transform)
 *       VKM_F(position)
 *       VKM_F(rotation)
 *       VKM_F(scale)
 *   VKM_REFLECT_END()
 *
 * Each VKM_F carries its own separator, so fields take no commas; an empty block
 * is a type with no reflected fields. NAME (see shortName) is what a behavior is
 * registered and serialized under. A field left out is not serialised.
 */
#define VKM_REFLECT_BEGIN(Type)                                                        \
    namespace Vkm::Engine::Reflect {                                                   \
    template<> struct Traits<Type> {                                                   \
        using vkm_reflect_self = Type;                                                 \
        static constexpr const char* NAME = ::Vkm::Engine::Reflect::shortName(#Type); \
        static constexpr auto fields() {                                               \
            return std::tuple_cat(

#define VKM_F(name) \
    std::make_tuple(::Vkm::Engine::Reflect::Field{#name, &vkm_reflect_self::name}),

#define VKM_REFLECT_END()                                                              \
                std::tuple<>()                                                         \
            );                                                                         \
        }                                                                              \
    };                                                                                 \
    }

/**
 * @brief Register an enum's value-ordered names in one place.
 *
 * Invoke at global scope, as VKM_REFLECT_BEGIN, with the names in value order:
 *
 *   VKM_ENUM_NAMES(::Vkm::Engine::LightType, "Directional", "Point", ...)
 *
 * The enum must end in `Count`, so a value added without a name fails to compile.
 */
#define VKM_ENUM_NAMES(EnumType, ...)                                            \
    namespace Vkm::Engine::Reflect {                                             \
    template<> struct EnumNames<EnumType> {                                      \
        static constexpr const char* const values[] = { __VA_ARGS__ };           \
        static constexpr std::size_t count = sizeof(values) / sizeof(values[0]); \
        static_assert(                                                           \
            count == static_cast<std::size_t>(EnumType::Count),                  \
            #EnumType " names out of sync with its Count sentinel"               \
        );                                                                       \
    };                                                                           \
    }
