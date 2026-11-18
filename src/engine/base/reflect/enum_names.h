#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace Vkm::Engine::Reflect {

/**
 * @brief Maps an enum to its value-ordered names.
 *
 * Specialized by VKM_ENUM_NAMES, exposing `values[]` (indexed by enum value)
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
 * @brief True iff Enum has a VKM_ENUM_NAMES registration.
 */
template<typename Enum, typename = void>
inline constexpr bool HAS_ENUM_NAMES = false;

template<typename Enum>
inline constexpr bool HAS_ENUM_NAMES<Enum, std::void_t<decltype(EnumNames<Enum>::count)>> = true;

} // namespace Vkm::Engine::Reflect

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
