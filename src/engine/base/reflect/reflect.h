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
 * @brief One (name, pointer-to-member) pair, the unit of compile-time field reflection, with what
 *        an inspector says about it.
 *
 * Templated on owner and member type so forEachField can deduce the field's type.
 */
template<typename T, typename M>
struct Field {
    std::string_view name;
    M T::*           ptr;
    float            min = 0.0f;     ///< The inspector's drag range; none when min == max. It bounds the
                                     ///< drag alone: a value set in code or read from a file is kept.
    float            max = 0.0f;     ///< The drag range's top, as min.
    const char*      tip = nullptr;  ///< What it is for, shown over its row.
};

// `Field{"position", &Transform::position}` deduces T=Transform, M=glm::vec3.
template<typename T, typename M>
Field(const char*, M T::*, float = 0.0f, float = 0.0f, const char* = nullptr) -> Field<T, M>;

/**
 * @brief What a reflect block's field list opens with, so each VKM_F can lead with its comma.
 */
struct FieldsBegin {};

/**
 * @brief A reflect block's fields as one tuple, built in one step: std::tuple_cat of a tuple per
 *        field instantiates as the square of the field count, in every file that includes it.
 */
template<typename... Fields>
constexpr std::tuple<Fields...> fieldTuple(FieldsBegin, Fields... fields) {
    return std::tuple<Fields...>(fields...);
}

/**
 * @brief The reflection trait, specialized per type by VKM_REFLECT_BEGIN / VKM_F / VKM_REFLECT_END.
 *
 * Exposes NAME (no namespace) and fields() -> std::tuple<Field<T, ...>...>.
 * Unspecialized use is a compile error.
 */
template<typename T>
struct Traits;

/**
 * @brief Visit every reflected field of @p obj.
 *
 * @tparam T  A type with a Traits specialization; const gives const field refs.
 * @tparam Fn Callable taking (std::string_view, field reference).
 * @param obj Object whose fields are visited.
 * @param fn  Called once per reflected field, in Traits order.
 */
template<typename T, typename Fn>
constexpr void forEachField(T& obj, Fn&& fn) {
    using Bare = std::remove_const_t<T>;
    constexpr auto tup = Traits<Bare>::fields();
    const auto visit   = [&](auto&&... f) { ((fn(f.name, obj.*(f.ptr))), ...); };
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
 * @brief True iff T has a Traits specialization.
 *
 * Lets a generic walker tell a nested reflected struct from a leaf.
 */
template<typename T, typename = void>
inline constexpr bool IS_REFLECTED = false;

template<typename T>
inline constexpr bool IS_REFLECTED<T, std::void_t<decltype(Traits<T>::fields())>> = true;

} // namespace Vkm::Engine::Reflect

/**
 * @brief Macro shorthand for declaring a Traits specialization.
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
 * A row is VKM_F(name), VKM_F(name, "tip") or VKM_F(name, min, max, "tip"), picked by how many
 * arguments it is given (the dummy last argument keeps VKM_F_PICK's list from ever being empty).
 * Each VKM_F carries its own separator, so fields take no commas; an empty block
 * is a type with no reflected fields. NAME (see shortName) is what a behavior is
 * registered and serialized under. A field left out is not serialized.
 */
#define VKM_REFLECT_BEGIN(Type) VKM_REFLECT_TRAITS(Type, )

/// VKM_REFLECT_BEGIN with @p Members declared in the Traits beside NAME; VKM_BEHAVIOR's.
#define VKM_REFLECT_TRAITS(Type, Members)                                              \
    namespace Vkm::Engine::Reflect {                                                   \
    template<> struct Traits<Type> {                                                   \
        using vkm_reflect_self = Type;                                                 \
        static constexpr const char* NAME = ::Vkm::Engine::Reflect::shortName(#Type); \
        Members                                                                        \
        static constexpr auto fields() {                                               \
            return ::Vkm::Engine::Reflect::fieldTuple(::Vkm::Engine::Reflect::FieldsBegin{}

#define VKM_F(...) \
    VKM_F_PICK(__VA_ARGS__, VKM_F_RANGE, VKM_F_RANGE_WANTS_A_TIP, VKM_F_TIP, VKM_F_NAME, )(__VA_ARGS__)
#define VKM_F_PICK(a, b, c, d, chosen, ...) chosen
#define VKM_F_NAME(name) VKM_F_RANGE(name, 0.0f, 0.0f, nullptr)
#define VKM_F_TIP(name, tip) VKM_F_RANGE(name, 0.0f, 0.0f, tip)
#define VKM_F_RANGE(name, min, max, tip) \
    , ::Vkm::Engine::Reflect::Field{#name, &vkm_reflect_self::name, min, max, tip}

#define VKM_REFLECT_END()                                                              \
            );                                                                         \
        }                                                                              \
    };                                                                                 \
    }
