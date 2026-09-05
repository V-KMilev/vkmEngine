#pragma once

#include <cstring>

#include <glm/gtx/easing.hpp>

#include "logger.h"

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
     * @brief Stable name -> function (deserialization).
     *
     * A name this build does not carry says so and falls back to linear, rather
     * than returning a curve the file did not ask for in silence.
     */
    inline EasingFunction byName(const char* name) {
        for (const Entry& e : EASINGS) {
            if (std::strcmp(name, e.name) == 0) return e.fn;
        }
        LOG_WARNING_C("ANIM", "No easing called '%s'; using linear", name);
        return &linear;
    }

    /**
     * @brief Function -> table index, or -1 for a function not in the table.
     *
     * Answers -1 rather than 0 because 0 is a real row: a miss and "linear"
     * would otherwise be the same answer, which is what let an unrecognised
     * curve serialize as linear with nothing said.
     *
     * A pointer can miss without anyone writing a custom curve. EASINGS is an
     * inline table of lambda addresses in a header, and vkm_core is a shared
     * library whose data carries no import annotation - so on Windows the
     * editor's copy of the table and the engine's hold different addresses for
     * the same curve, and a pointer chosen in one is unrecognisable in the
     * other. Linux binds them to one copy and never sees it.
     */
    inline int indexOf(EasingFunction f) {
        for (int i = 0; i < EASING_COUNT; ++i) {
            if (EASINGS[i].fn == f) return i;
        }
        return -1;
    }

    /**
     * @brief Function -> stable name, for serialization.
     *
     * A function the table does not know is written as linear, because a name
     * is what the format carries and there is no other one to give it - but it
     * says so first, since the curve an author chose is about to be lost.
     */
    inline const char* nameOf(EasingFunction f) {
        const int i = indexOf(f);
        if (i < 0) {
            LOG_WARNING_C("ANIM", "An easing that is not in this build's table is being saved "
                        "as linear; the curve it names will not come back");
            return EASINGS[0].name;
        }
        return EASINGS[i].name;
    }

} // namespace Easing

} // namespace Vkm::Engine
