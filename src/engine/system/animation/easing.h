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

    /// Function-pointer -> stable string name (for serialization).
    /// Returns "linear" if the pointer is unknown.
    inline const char* nameOf(EasingFunction f) {
        if (f == &linear)             return "linear";
        if (f == &easeInQuad)         return "easeInQuad";
        if (f == &easeOutQuad)        return "easeOutQuad";
        if (f == &easeInOutQuad)      return "easeInOutQuad";
        if (f == &easeInCubic)        return "easeInCubic";
        if (f == &easeOutCubic)       return "easeOutCubic";
        if (f == &easeInOutCubic)     return "easeInOutCubic";
        if (f == &easeInQuart)        return "easeInQuart";
        if (f == &easeOutQuart)       return "easeOutQuart";
        if (f == &easeInOutQuart)     return "easeInOutQuart";
        if (f == &easeInQuint)        return "easeInQuint";
        if (f == &easeOutQuint)       return "easeOutQuint";
        if (f == &easeInOutQuint)     return "easeInOutQuint";
        if (f == &easeInSine)         return "easeInSine";
        if (f == &easeOutSine)        return "easeOutSine";
        if (f == &easeInOutSine)      return "easeInOutSine";
        if (f == &easeInExpo)         return "easeInExpo";
        if (f == &easeOutExpo)        return "easeOutExpo";
        if (f == &easeInOutExpo)      return "easeInOutExpo";
        if (f == &easeInCirc)         return "easeInCirc";
        if (f == &easeOutCirc)        return "easeOutCirc";
        if (f == &easeInOutCirc)      return "easeInOutCirc";
        if (f == &easeInBack)         return "easeInBack";
        if (f == &easeOutBack)        return "easeOutBack";
        if (f == &easeInOutBack)      return "easeInOutBack";
        if (f == &easeInElastic)      return "easeInElastic";
        if (f == &easeOutElastic)     return "easeOutElastic";
        if (f == &easeInOutElastic)   return "easeInOutElastic";
        if (f == &easeInBounce)       return "easeInBounce";
        if (f == &easeOutBounce)      return "easeOutBounce";
        if (f == &easeInOutBounce)    return "easeInOutBounce";
        return "linear";
    }

    /// Stable string name -> function pointer (for deserialization).
    /// Falls back to linear if the name is unknown.
    inline EasingFunction byName(const char* name) {
        #define VKM_EASING_MATCH(fn) if (std::strcmp(name, #fn) == 0) return &fn
        VKM_EASING_MATCH(linear);
        VKM_EASING_MATCH(easeInQuad);   VKM_EASING_MATCH(easeOutQuad);   VKM_EASING_MATCH(easeInOutQuad);
        VKM_EASING_MATCH(easeInCubic);  VKM_EASING_MATCH(easeOutCubic);  VKM_EASING_MATCH(easeInOutCubic);
        VKM_EASING_MATCH(easeInQuart);  VKM_EASING_MATCH(easeOutQuart);  VKM_EASING_MATCH(easeInOutQuart);
        VKM_EASING_MATCH(easeInQuint);  VKM_EASING_MATCH(easeOutQuint);  VKM_EASING_MATCH(easeInOutQuint);
        VKM_EASING_MATCH(easeInSine);   VKM_EASING_MATCH(easeOutSine);   VKM_EASING_MATCH(easeInOutSine);
        VKM_EASING_MATCH(easeInExpo);   VKM_EASING_MATCH(easeOutExpo);   VKM_EASING_MATCH(easeInOutExpo);
        VKM_EASING_MATCH(easeInCirc);   VKM_EASING_MATCH(easeOutCirc);   VKM_EASING_MATCH(easeInOutCirc);
        VKM_EASING_MATCH(easeInBack);   VKM_EASING_MATCH(easeOutBack);   VKM_EASING_MATCH(easeInOutBack);
        VKM_EASING_MATCH(easeInElastic); VKM_EASING_MATCH(easeOutElastic); VKM_EASING_MATCH(easeInOutElastic);
        VKM_EASING_MATCH(easeInBounce); VKM_EASING_MATCH(easeOutBounce); VKM_EASING_MATCH(easeInOutBounce);
        #undef VKM_EASING_MATCH
        return &linear;
    }

    /// All easing function names in display order, grouped by family.
    /// Used to drive UI dropdowns; resolve to a function with byName().
    inline constexpr const char* kEasingNames[] = {
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

    inline constexpr int kEasingCount =
        static_cast<int>(sizeof(kEasingNames) / sizeof(kEasingNames[0]));

    /// Index into kEasingNames -> function pointer (clamped to linear).
    inline EasingFunction byIndex(int i) {
        if (i < 0 || i >= kEasingCount) return &linear;
        return byName(kEasingNames[i]);
    }

    /// Function pointer -> index into kEasingNames (0/linear if unknown).
    inline int indexOf(EasingFunction f) {
        const char* n = nameOf(f);
        for (int i = 0; i < kEasingCount; ++i) {
            if (std::strcmp(n, kEasingNames[i]) == 0) return i;
        }
        return 0;
    }
} // namespace Easing

} // namespace Engine
