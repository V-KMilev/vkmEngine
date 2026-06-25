#pragma once

#include <cstring>

#include <glm/gtx/easing.hpp>

namespace Engine {

/** @brief Type alias for easing function pointers.
 * An easing function takes a float from 0 to 1 and returns a float from 0 to 1.
 */
using EasingFunction = float (*)(float);

/** @brief Namespace containing common easing/interpolation functions.
 * These wrappers call corresponding GLM easing routines.
 * We use function pointers to avoid the complexity of template functions.
 * All easing functions accept t in [0, 1] and produce a value in [0, 1].
 */
namespace Easing {
    // Linear
    inline float linear(float t) {
        return glm::linearInterpolation(t);
    }

    // Quadratic
    inline float easeInQuad(float t) {
        return glm::quadraticEaseIn(t);
    }
    inline float easeOutQuad(float t) {
        return glm::quadraticEaseOut(t);
    }
    inline float easeInOutQuad(float t) {
        return glm::quadraticEaseInOut(t);
    }

    // Cubic
    inline float easeInCubic(float t) {
        return glm::cubicEaseIn(t);
    }
    inline float easeOutCubic(float t) {
        return glm::cubicEaseOut(t);
    }
    inline float easeInOutCubic(float t) {
        return glm::cubicEaseInOut(t);
    }

    // Quartic
    inline float easeInQuart(float t) {
        return glm::quarticEaseIn(t);
    }
    inline float easeOutQuart(float t) {
        return glm::quarticEaseOut(t);
    }
    inline float easeInOutQuart(float t) {
        return glm::quarticEaseInOut(t);
    }

    // Quintic
    inline float easeInQuint(float t) {
        return glm::quinticEaseIn(t);
    }
    inline float easeOutQuint(float t) {
        return glm::quinticEaseOut(t);
    }
    inline float easeInOutQuint(float t) {
        return glm::quinticEaseInOut(t);
    }

    // Sine
    inline float easeInSine(float t) {
        return glm::sineEaseIn(t);
    }
    inline float easeOutSine(float t) {
        return glm::sineEaseOut(t);
    }
    inline float easeInOutSine(float t) {
        return glm::sineEaseInOut(t);
    }

    // Exponential
    inline float easeInExpo(float t) {
        return glm::exponentialEaseIn(t);
    }
    inline float easeOutExpo(float t) {
        return glm::exponentialEaseOut(t);
    }
    inline float easeInOutExpo(float t) {
        return glm::exponentialEaseInOut(t);
    }

    // Circular
    inline float easeInCirc(float t) {
        return glm::circularEaseIn(t);
    }
    inline float easeOutCirc(float t) {
        return glm::circularEaseOut(t);
    }
    inline float easeInOutCirc(float t) {
        return glm::circularEaseInOut(t);
    }

    // Back
    inline float easeInBack(float t) {
        return glm::backEaseIn(t);
    }
    inline float easeOutBack(float t) {
        return glm::backEaseOut(t);
    }
    inline float easeInOutBack(float t) {
        return glm::backEaseInOut(t);
    }

    // Elastic
    inline float easeInElastic(float t) {
        return glm::elasticEaseIn(t);
    }
    inline float easeOutElastic(float t) {
        return glm::elasticEaseOut(t);
    }
    inline float easeInOutElastic(float t) {
        return glm::elasticEaseInOut(t);
    }

    // Bounce
    inline float easeInBounce(float t) {
        return glm::bounceEaseIn(t);
    }
    inline float easeOutBounce(float t) {
        return glm::bounceEaseOut(t);
    }
    inline float easeInOutBounce(float t) {
        return glm::bounceEaseInOut(t);
    }

    /**
     * @brief All easing function names in display order, grouped by family. Drives UI
     * dropdowns (resolve with byIndex/byName). Index-aligned with EASING_FNS
     * below - add to BOTH when adding an easing (the static_assert guards it).
     */
    inline constexpr const char* EASING_NAMES[] = {
        "linear",
        "easeInQuad",    "easeOutQuad",    "easeInOutQuad",
        "easeInCubic",   "easeOutCubic",   "easeInOutCubic",
        "easeInQuart",   "easeOutQuart",   "easeInOutQuart",
        "easeInQuint",   "easeOutQuint",   "easeInOutQuint",
        "easeInSine",    "easeOutSine",    "easeInOutSine",
        "easeInExpo",    "easeOutExpo",    "easeInOutExpo",
        "easeInCirc",    "easeOutCirc",    "easeInOutCirc",
        "easeInBack",    "easeOutBack",    "easeInOutBack",
        "easeInElastic", "easeOutElastic", "easeInOutElastic",
        "easeInBounce",  "easeOutBounce",  "easeInOutBounce",
    };

    /**
     * @brief Function pointers, index-aligned with EASING_NAMES above. The four
     * lookups below read these two parallel arrays, so each (name, function)
     * pairing lives in exactly one place instead of being hand-listed thrice.
     */
    inline constexpr EasingFunction EASING_FNS[] = {
        &linear,
        &easeInQuad,    &easeOutQuad,    &easeInOutQuad,
        &easeInCubic,   &easeOutCubic,   &easeInOutCubic,
        &easeInQuart,   &easeOutQuart,   &easeInOutQuart,
        &easeInQuint,   &easeOutQuint,   &easeInOutQuint,
        &easeInSine,    &easeOutSine,    &easeInOutSine,
        &easeInExpo,    &easeOutExpo,    &easeInOutExpo,
        &easeInCirc,    &easeOutCirc,    &easeInOutCirc,
        &easeInBack,    &easeOutBack,    &easeInOutBack,
        &easeInElastic, &easeOutElastic, &easeInOutElastic,
        &easeInBounce,  &easeOutBounce,  &easeInOutBounce,
    };

    inline constexpr int EASING_COUNT =
        static_cast<int>(sizeof(EASING_NAMES) / sizeof(EASING_NAMES[0]));
    static_assert(sizeof(EASING_FNS) / sizeof(EASING_FNS[0]) == static_cast<size_t>(EASING_COUNT),
                  "EASING_NAMES and EASING_FNS must stay index-aligned");

    /** @brief Index into the tables -> function pointer (clamped to linear). */
    inline EasingFunction byIndex(int i) {
        if (i < 0 || i >= EASING_COUNT) return &linear;
        return EASING_FNS[i];
    }

    /**
     * @brief Stable string name -> function pointer (for deserialization).
     * Falls back to linear if the name is unknown.
     */
    inline EasingFunction byName(const char* name) {
        for (int i = 0; i < EASING_COUNT; ++i) {
            if (std::strcmp(name, EASING_NAMES[i]) == 0) return EASING_FNS[i];
        }
        return &linear;
    }

    /** @brief Function pointer -> index into the tables (0/linear if unknown). */
    inline int indexOf(EasingFunction f) {
        for (int i = 0; i < EASING_COUNT; ++i) {
            if (EASING_FNS[i] == f) return i;
        }
        return 0;
    }

    /**
     * @brief Function-pointer -> stable string name (for serialization).
     * Returns "linear" if the pointer is unknown.
     */
    inline const char* nameOf(EasingFunction f) {
        return EASING_NAMES[indexOf(f)];
    }
} // namespace Easing

} // namespace Engine
