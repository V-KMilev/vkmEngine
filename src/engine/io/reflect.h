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
 * @brief Enum value -> its serialized / display name.
 *
 * @p names lists the names in enum-value order (the value is the index); an
 * out-of-range value falls back to the first name. Pairing this with one
 * names[] array declared next to the enum gives serialization and editor
 * combos a single source of truth that cannot drift.
 */
template<typename Enum, std::size_t N>
constexpr const char* enumName(Enum value, const char* const (&names)[N]) {
    const auto index = static_cast<std::size_t>(value);
    return index < N ? names[index] : names[0];
}

/**
 * @brief Parse an enum from a name, falling back to value 0 when unknown.
 */
template<typename Enum, std::size_t N>
Enum enumFromName(std::string_view name, const char* const (&names)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (name == names[i]) return static_cast<Enum>(i);
    }
    return static_cast<Enum>(0);
}

} // namespace Engine::Reflect

/**
 * @brief Macro shorthand for declaring a Traits specialisation.
 *
 * Usage (outside any namespace):
 *
 *   VKM_REFLECT_BEGIN(Engine::Transform)
 *       VKM_F(position),
 *       VKM_F(rotation),
 *       VKM_F(scale)
 *   VKM_REFLECT_END()
 *
 * VKM_F() looks up the type alias `vkm_reflect_self` injected by
 * VKM_REFLECT_BEGIN so each field doesn't have to repeat the type name.
 *
 * Fields omitted from the macro are NOT serialised - that's the
 * mechanism for "internal only" data (e.g. ReflectionProbe::bakeVersion).
 */
#define VKM_REFLECT_BEGIN(Type)                                              \
    namespace Engine::Reflect {                                              \
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
