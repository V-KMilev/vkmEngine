#pragma once

// The one way to write a test, in tests/<area>/<topic>.cpp:
//
//     TEST("a saved scene loads back the same") {
//         ...
//         CHECK(loaded == saved);
//         CHECK_NEAR(body.position.y, 0.5f, 0.01f);
//     }
//
// The name is the promise; the area is the folder the file sits in. Each test runs in a process of
// its own (main.cpp), so a crash or a hang is that test's failure and nothing else's.
//
// CHECK prints the expression and, for a comparison, both sides; it goes on, so one run shows every
// check that failed. REQUIRE stops the test, for a check the rest of it cannot run without.

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"

namespace Vkm::Test {

using Body = void (*)();

/**
 * @brief Adds the test that follows to the run, at static initialization.
 */
struct Registration {
    /**
     * @param name    What works, as the test's name says.
     * @param file    Its file, whose folder under tests/ is its area.
     * @param body    The test.
     * @param infoLog Whether its log keeps Info lines too (TEST_WITH_INFO_LOG).
     */
    Registration(const char* name, const char* file, Body body, bool infoLog = false);
};

/**
 * @brief Thrown by REQUIRE, caught by main: the test ends there, failed.
 */
struct Stop {};

/**
 * @brief Count a failed check and say where, what and with which values.
 *
 * @param file The check's file.
 * @param line Its line.
 * @param what The expression as written, and the values it saw.
 */
void fail(const char* file, int line, const std::string& what);

namespace detail {

template<typename T, typename = void>
inline constexpr bool STREAMS = false;
template<typename T>
inline constexpr bool STREAMS<T, std::void_t<decltype(
    std::declval<std::ostream&>() << std::declval<const T&>()
)>> =
    true;

} // namespace detail

/**
 * @brief A value as a failed check prints it.
 *
 * @param value Anything; what cannot be printed shows as "?".
 * @return Its text.
 */
template<typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out.precision(9);
    if constexpr (std::is_same_v<T, bool>) {
        out << (value ? "true" : "false");
    } else if constexpr (std::is_enum_v<T>) {
        out << static_cast<long long>(value);
    } else if constexpr (std::is_same_v<T, Vkm::Engine::EntityId>) {
        out << "entity " << value.slot() << "." << value.generation();
    } else if constexpr (std::is_same_v<T, glm::quat>) {
        out << "quat(" << value.w << ", " << value.x << ", " << value.y << ", " << value.z << ")";
    } else if constexpr (
        std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>
    ) {
        out << "(";
        for (int i = 0; i < T::length(); ++i) out << (i ? ", " : "") << value[i];
        out << ")";
    } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
        out << '"' << value << '"';
    } else if constexpr (std::is_convertible_v<T, const char*>) {
        const char* text = value;
        out << (text ? text : "null");
    } else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
        out << static_cast<int>(value);
    } else if constexpr (detail::STREAMS<T>) {
        out << value;
    } else {
        out << "?";
    }
    return out.str();
}

namespace detail {

// CHECK(a == b) expands to `Decompose{} <= a == b`, which C++ groups as `(Decompose{} <= a) == b`,
// so both sides are kept and printed when the check fails. A test compares a size with a count as
// written, so the sign warning is off for the comparison alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
template<typename L>
struct Left {
    const L& value;

#define VKM_TEST_COMPARE(op)                                                              \
    template<typename R>                                                                  \
    std::pair<bool, std::string> operator op(const R& right) const {                      \
        return {static_cast<bool>(value op right), show(value) + " " #op " " + show(right)}; \
    }
    VKM_TEST_COMPARE(==)
    VKM_TEST_COMPARE(!=)
    VKM_TEST_COMPARE(<)
    VKM_TEST_COMPARE(<=)
    VKM_TEST_COMPARE(>)
    VKM_TEST_COMPARE(>=)
#undef VKM_TEST_COMPARE
};
#pragma GCC diagnostic pop

struct Decompose {
    template<typename L>
    Left<L> operator<=(const L& value) const { return {value}; }
};

template<typename L>
std::pair<bool, std::string> verdict(const Left<L>& alone) {
    return {static_cast<bool>(alone.value), show(alone.value)};
}

inline std::pair<bool, std::string> verdict(const std::pair<bool, std::string>& compared) { return compared; }

/// How far apart two values are, by their largest component.
template<typename T>
float distance(const T& a, const T& b) {
    if constexpr (std::is_arithmetic_v<T>) {
        return static_cast<float>(std::fabs(static_cast<double>(a) - static_cast<double>(b)));
    } else if constexpr (std::is_same_v<T, glm::quat>) {
        // q and -q are one rotation.
        return std::min(
            glm::length(glm::vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w)),
            glm::length(glm::vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w))
        );
    } else {
        float most = 0.0f;
        for (int i = 0; i < T::length(); ++i) {
            const float apart = std::fabs(a[i] - b[i]);
            if (std::isnan(apart)) return apart;  // std::max would drop it, and NaN is near nothing
            most = std::max(most, apart);
        }
        return most;
    }
}

} // namespace detail

/// Report @p verdict failed, for CHECK and REQUIRE.
inline bool record(
    const char* file,
    int line,
    const char* macro,
    const char* expression,
    const std::pair<bool, std::string>& verdict
) {
    if (verdict.first) return true;
    fail(file, line, std::string(macro) + "(" + expression + ") with " + verdict.second);
    return false;
}

template<typename A, typename B>
bool near(
    const char* file,
    int line,
    const char* a,
    const char* b,
    const A& left,
    const B& right,
    float tolerance
) {
    using T = std::common_type_t<A, B>;
    const float apart = detail::distance<T>(static_cast<T>(left), static_cast<T>(right));
    if (apart <= tolerance) return true;
    fail(file, line,
         std::string("CHECK_NEAR(") + a + ", " + b + ") with " + show(static_cast<T>(left)) + " and "
             + show(static_cast<T>(right)) + ", " + show(apart) + " apart, more than " + show(tolerance));
    return false;
}

} // namespace Vkm::Test

#define VKM_TEST_JOIN2(a, b) a##b
#define VKM_TEST_JOIN(a, b)  VKM_TEST_JOIN2(a, b)

#define TEST(name)                                                                              \
    static void VKM_TEST_JOIN(vkmTest, __LINE__)();                                             \
    static const ::Vkm::Test::Registration VKM_TEST_JOIN(vkmTestAt, __LINE__)(                  \
        name, __FILE__, &VKM_TEST_JOIN(vkmTest, __LINE__));                                     \
    static void VKM_TEST_JOIN(vkmTest, __LINE__)()

// A TEST whose log keeps Info lines too, for one that checks what the engine says at Info.
#define TEST_WITH_INFO_LOG(name)                                                                \
    static void VKM_TEST_JOIN(vkmTest, __LINE__)();                                             \
    static const ::Vkm::Test::Registration VKM_TEST_JOIN(vkmTestAt, __LINE__)(                  \
        name, __FILE__, &VKM_TEST_JOIN(vkmTest, __LINE__), true);                               \
    static void VKM_TEST_JOIN(vkmTest, __LINE__)()

// Statements, so the grouping the decomposition relies on - what -Wparentheses asks about - can be
// allowed around them.
#define VKM_TEST_RECORD(macro, ...)                                                             \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wparentheses\"")           \
    const bool vkmTestHeld = ::Vkm::Test::record(__FILE__, __LINE__, macro, #__VA_ARGS__,      \
        ::Vkm::Test::detail::verdict(::Vkm::Test::detail::Decompose{} <= __VA_ARGS__));         \
    _Pragma("GCC diagnostic pop")

#define CHECK(...)                                                                              \
    do {                                                                                        \
        VKM_TEST_RECORD("CHECK", __VA_ARGS__)                                                   \
        (void)vkmTestHeld;                                                                      \
    } while (false)

#define REQUIRE(...)                                                                            \
    do {                                                                                        \
        VKM_TEST_RECORD("REQUIRE", __VA_ARGS__)                                                 \
        if (!vkmTestHeld) throw ::Vkm::Test::Stop{};                                            \
    } while (false)

#define CHECK_NEAR(a, b, tolerance) ::Vkm::Test::near(__FILE__, __LINE__, #a, #b, (a), (b), (tolerance))
