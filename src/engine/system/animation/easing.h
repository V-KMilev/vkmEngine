#pragma once

#include <cstring>

#include <glm/gtx/easing.hpp>

namespace Engine {

/**
 * @brief Type alias for easing function pointers.
 *
 * An easing function takes a float from 0 to 1 and returns a float from 0 to 1.
 */
using EasingFunction = float (*)(float);

/**
 * @brief Namespace containing common easing/interpolation functions.
 *
 * These call the corresponding GLM easing routines.
 * We use function pointers to avoid the complexity of template functions.
 * All easing functions accept t in [0, 1] and produce a value in [0, 1].
 */
namespace Easing {
    // linear is the canonical fallback (used as a default elsewhere), so it keeps a
    // name; every other entry lives only as a lambda in EASING_FNS below. The glm::*
    // routines are templates whose address can't be taken, so the table wraps each in
    // a non-capturing lambda decayed to float(*)(float) via the unary + operator.
    inline float linear(float t) {
        return glm::linearInterpolation(t);
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
        +[](float t) { return glm::quadraticEaseIn(t); },
        +[](float t) { return glm::quadraticEaseOut(t); },
        +[](float t) { return glm::quadraticEaseInOut(t); },
        +[](float t) { return glm::cubicEaseIn(t); },
        +[](float t) { return glm::cubicEaseOut(t); },
        +[](float t) { return glm::cubicEaseInOut(t); },
        +[](float t) { return glm::quarticEaseIn(t); },
        +[](float t) { return glm::quarticEaseOut(t); },
        +[](float t) { return glm::quarticEaseInOut(t); },
        +[](float t) { return glm::quinticEaseIn(t); },
        +[](float t) { return glm::quinticEaseOut(t); },
        +[](float t) { return glm::quinticEaseInOut(t); },
        +[](float t) { return glm::sineEaseIn(t); },
        +[](float t) { return glm::sineEaseOut(t); },
        +[](float t) { return glm::sineEaseInOut(t); },
        +[](float t) { return glm::exponentialEaseIn(t); },
        +[](float t) { return glm::exponentialEaseOut(t); },
        +[](float t) { return glm::exponentialEaseInOut(t); },
        +[](float t) { return glm::circularEaseIn(t); },
        +[](float t) { return glm::circularEaseOut(t); },
        +[](float t) { return glm::circularEaseInOut(t); },
        +[](float t) { return glm::backEaseIn(t); },
        +[](float t) { return glm::backEaseOut(t); },
        +[](float t) { return glm::backEaseInOut(t); },
        +[](float t) { return glm::elasticEaseIn(t); },
        +[](float t) { return glm::elasticEaseOut(t); },
        +[](float t) { return glm::elasticEaseInOut(t); },
        +[](float t) { return glm::bounceEaseIn(t); },
        +[](float t) { return glm::bounceEaseOut(t); },
        +[](float t) { return glm::bounceEaseInOut(t); },
    };

    inline constexpr int EASING_COUNT =
        static_cast<int>(sizeof(EASING_NAMES) / sizeof(EASING_NAMES[0]));
    static_assert(sizeof(EASING_FNS) / sizeof(EASING_FNS[0]) == static_cast<size_t>(EASING_COUNT),
                  "EASING_NAMES and EASING_FNS must stay index-aligned");

    /**
     * @brief Map a table index to its easing function pointer.
     *
     * An out-of-range index is clamped to the linear easing function.
     *
     * @param i Index into the easing tables.
     * @return The matching easing function, or linear if @p i is out of range.
     */
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

    /**
     * @brief Map an easing function pointer back to its table index.
     *
     * @param f The easing function to look up.
     * @return The table index of @p f, or 0 (linear) if it is not a known function.
     */
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
