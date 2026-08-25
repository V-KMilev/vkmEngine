#pragma once

#include <cstring>

#include <glm/gtx/easing.hpp>

namespace Vkm::Engine {

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

    /// A real named function, not an EASE() row: it is the default easing elsewhere.
    inline float linear(float t) { return glm::linearInterpolation(t); }

    /**
     * @brief Wrap a glm easing routine as a plain EasingFunction pointer.
     *
     * The glm easings are templates over `genType const&`, so no pointer to one
     * carries EasingFunction's `float(float)` signature. Each is wrapped in a
     * captureless lambda instead, and the leading unary + is what decays that
     * lambda to a function pointer the table below can hold.
     *
     * @param glmFn Unqualified name of the glm routine, e.g. quadraticEaseIn.
     */
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

} // namespace Vkm::Engine
