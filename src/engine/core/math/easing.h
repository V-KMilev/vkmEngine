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
 * @brief Common easing/interpolation functions over the GLM easing routines.
 *
 * Function pointers (not templates) so an easing can be stored on a track and
 * round-tripped by name. All accept t in [0, 1] and produce a value in [0, 1].
 */
namespace Easing {

    // linear keeps a real named function because it is the canonical fallback /
    // default elsewhere. The rest are thin lambdas over the glm::* easing
    // templates (whose addresses can't be taken directly), decayed to a plain
    // function pointer via the unary +; EASE() spells that wrapper.
    inline float linear(float t) { return glm::linearInterpolation(t); }

    #define EASE(glmFn) +[](float t) { return glm::glmFn(t); }

    /**
     * @brief One easing: its stable name (for the UI dropdown + serialization)
     *        paired with its function pointer.
     */
    struct Entry {
        const char*    name;
        EasingFunction fn;
    };

    /**
     * @brief Every easing in display order, grouped by family. The single source
     * of truth: name and function live in one row, so they can't drift apart.
     * Add an easing by adding a row; the lookups below cover the rest.
     */
    inline constexpr Entry EASINGS[] = {
        {"linear",          &linear},

        {"easeInQuad",      EASE(quadraticEaseIn)},
        {"easeOutQuad",     EASE(quadraticEaseOut)},
        {"easeInOutQuad",   EASE(quadraticEaseInOut)},

        {"easeInCubic",     EASE(cubicEaseIn)},
        {"easeOutCubic",    EASE(cubicEaseOut)},
        {"easeInOutCubic",  EASE(cubicEaseInOut)},

        {"easeInQuart",     EASE(quarticEaseIn)},
        {"easeOutQuart",    EASE(quarticEaseOut)},
        {"easeInOutQuart",  EASE(quarticEaseInOut)},

        {"easeInQuint",     EASE(quinticEaseIn)},
        {"easeOutQuint",    EASE(quinticEaseOut)},
        {"easeInOutQuint",  EASE(quinticEaseInOut)},

        {"easeInSine",      EASE(sineEaseIn)},
        {"easeOutSine",     EASE(sineEaseOut)},
        {"easeInOutSine",   EASE(sineEaseInOut)},

        {"easeInExpo",      EASE(exponentialEaseIn)},
        {"easeOutExpo",     EASE(exponentialEaseOut)},
        {"easeInOutExpo",   EASE(exponentialEaseInOut)},

        {"easeInCirc",      EASE(circularEaseIn)},
        {"easeOutCirc",     EASE(circularEaseOut)},
        {"easeInOutCirc",   EASE(circularEaseInOut)},

        {"easeInBack",      EASE(backEaseIn)},
        {"easeOutBack",     EASE(backEaseOut)},
        {"easeInOutBack",   EASE(backEaseInOut)},

        {"easeInElastic",   EASE(elasticEaseIn)},
        {"easeOutElastic",  EASE(elasticEaseOut)},
        {"easeInOutElastic",EASE(elasticEaseInOut)},

        {"easeInBounce",    EASE(bounceEaseIn)},
        {"easeOutBounce",   EASE(bounceEaseOut)},
        {"easeInOutBounce", EASE(bounceEaseInOut)},
    };

    #undef EASE

    inline constexpr int EASING_COUNT = static_cast<int>(sizeof(EASINGS) / sizeof(EASINGS[0]));

    /**
     * @brief Map a table index to its easing function; out-of-range -> linear.
     */
    inline EasingFunction byIndex(int i) {
        return (i < 0 || i >= EASING_COUNT) ? &linear : EASINGS[i].fn;
    }

    /**
     * @brief Stable name -> function (deserialization); unknown -> linear.
     */
    inline EasingFunction byName(const char* name) {
        for (const Entry& e : EASINGS) {
            if (std::strcmp(name, e.name) == 0) return e.fn;
        }
        return &linear;
    }

    /**
     * @brief Function -> table index; unknown -> 0 (linear).
     */
    inline int indexOf(EasingFunction f) {
        for (int i = 0; i < EASING_COUNT; ++i) {
            if (EASINGS[i].fn == f) return i;
        }
        return 0;
    }

    /**
     * @brief Function -> stable name (serialization); unknown -> "linear".
     */
    inline const char* nameOf(EasingFunction f) {
        return EASINGS[indexOf(f)].name;
    }

} // namespace Easing

} // namespace Engine
