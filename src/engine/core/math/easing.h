#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

#include <glm/gtx/easing.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Type alias for easing function pointers.
 *
 * Maps 0 to 0 and 1 to 1. Back and Elastic overshoot [0, 1] between, so a
 * reader that needs the range clamps.
 */
using EasingFunction = float (*)(float);

/**
 * @brief Every easing in display order: its enumerator, stable name and glm routine.
 *
 * Add an easing by adding a row; the enum, its names and its curves follow from it.
 */
#define VKM_EASINGS(X)                                                    \
    X(Linear,           "linear",           linearInterpolation)          \
                                                                          \
    X(EaseInQuad,       "easeInQuad",       quadraticEaseIn)              \
    X(EaseOutQuad,      "easeOutQuad",      quadraticEaseOut)             \
    X(EaseInOutQuad,    "easeInOutQuad",    quadraticEaseInOut)           \
                                                                          \
    X(EaseInCubic,      "easeInCubic",      cubicEaseIn)                  \
    X(EaseOutCubic,     "easeOutCubic",     cubicEaseOut)                 \
    X(EaseInOutCubic,   "easeInOutCubic",   cubicEaseInOut)               \
                                                                          \
    X(EaseInQuart,      "easeInQuart",      quarticEaseIn)                \
    X(EaseOutQuart,     "easeOutQuart",     quarticEaseOut)               \
    X(EaseInOutQuart,   "easeInOutQuart",   quarticEaseInOut)             \
                                                                          \
    X(EaseInQuint,      "easeInQuint",      quinticEaseIn)                \
    X(EaseOutQuint,     "easeOutQuint",     quinticEaseOut)               \
    X(EaseInOutQuint,   "easeInOutQuint",   quinticEaseInOut)             \
                                                                          \
    X(EaseInSine,       "easeInSine",       sineEaseIn)                   \
    X(EaseOutSine,      "easeOutSine",      sineEaseOut)                  \
    X(EaseInOutSine,    "easeInOutSine",    sineEaseInOut)                \
                                                                          \
    X(EaseInExpo,       "easeInExpo",       exponentialEaseIn)            \
    X(EaseOutExpo,      "easeOutExpo",      exponentialEaseOut)           \
    X(EaseInOutExpo,    "easeInOutExpo",    exponentialEaseInOut)         \
                                                                          \
    X(EaseInCirc,       "easeInCirc",       circularEaseIn)               \
    X(EaseOutCirc,      "easeOutCirc",      circularEaseOut)              \
    X(EaseInOutCirc,    "easeInOutCirc",    circularEaseInOut)            \
                                                                          \
    X(EaseInBack,       "easeInBack",       backEaseIn)                   \
    X(EaseOutBack,      "easeOutBack",      backEaseOut)                  \
    X(EaseInOutBack,    "easeInOutBack",    backEaseInOut)                \
                                                                          \
    X(EaseInElastic,    "easeInElastic",    elasticEaseIn)                \
    X(EaseOutElastic,   "easeOutElastic",   elasticEaseOut)               \
    X(EaseInOutElastic, "easeInOutElastic", elasticEaseInOut)             \
                                                                          \
    X(EaseInBounce,     "easeInBounce",     bounceEaseIn)                 \
    X(EaseOutBounce,    "easeOutBounce",    bounceEaseOut)                \
    X(EaseInOutBounce,  "easeInOutBounce",  bounceEaseInOut)

/**
 * @brief An easing curve; the scene file stores its VKM_ENUM_NAMES name.
 *
 * The enumerator is the identity, never the function pointer: easingFunction
 * resolves it at the call. On Windows the editor's copy of an inline table and
 * the shared vkm_core's hold different addresses.
 */
enum class Easing : uint8_t {
#define VKM_EASING_ID(id, name, fn) id,
    VKM_EASINGS(VKM_EASING_ID)
#undef VKM_EASING_ID
    Count
};

/**
 * @brief Resolve an easing to the function that computes it.
 *
 * Call where the curve is evaluated, so the pointer is this module's own.
 *
 * @param easing The curve.
 * @return Its function pointer; linear when @p easing is out of range.
 */
inline EasingFunction easingFunction(Easing easing) {
    // glm's easings are templates; the unary + decays each wrapping lambda to a pointer.
    static constexpr EasingFunction FUNCTIONS[] = {
#define VKM_EASING_FN(id, name, fn) +[](float t) { return glm::fn(t); },
        VKM_EASINGS(VKM_EASING_FN)
#undef VKM_EASING_FN
    };
    static_assert(std::size(FUNCTIONS) == static_cast<std::size_t>(Easing::Count));
    const auto index = static_cast<std::size_t>(easing);
    return index < std::size(FUNCTIONS) ? FUNCTIONS[index] : FUNCTIONS[0];
}

} // namespace Vkm::Engine

#define VKM_EASING_NAME(id, name, fn) name,
VKM_ENUM_NAMES(::Vkm::Engine::Easing, VKM_EASINGS(VKM_EASING_NAME))
#undef VKM_EASING_NAME

#undef VKM_EASINGS
