#pragma once

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "core/math/random.h"

namespace Vkm::Engine::Math {

/**
 * @brief A value drawn once, uniformly between min and max; equal ends are a constant.
 */
struct FloatRange {
    float min = 0.0f;
    float max = 0.0f;

    /**
     * @brief One draw from the range.
     *
     * @param rng The stream it is drawn from: a constant draws nothing, so it never moves the stream.
     * @return A value in [min, max].
     */
    float draw(Rng& rng) const { return min == max ? min : rng.nextFloat(min, max); }
};

/**
 * @brief The value of @p keys at @p t: straight between the nearest key either side, the first
 *        and last held past them.
 *
 * In any order: of keys at one time, the last listed holds from that time on.
 *
 * @tparam Key   A key with a `time`.
 * @tparam Value What a key reads as; glm::mix blends two.
 * @tparam Read  Callable taking a `const Key&` and returning its Value.
 * @param keys  Keys, each with a `time`.
 * @param t     Where in the span.
 * @param none  What no keys read.
 * @param value Reads a key's value.
 * @return The value there.
 */
template <typename Key, typename Value, typename Read>
Value sampleKeys(const std::vector<Key>& keys, float t, const Value& none, Read&& value) {
    const Key* below = nullptr;
    const Key* above = nullptr;
    for (const Key& key : keys) {
        if (key.time <= t && (!below || key.time >= below->time)) below = &key;
        if (key.time >= t && (!above || key.time < above->time)) above = &key;
    }
    if (!below && !above) return none;
    if (!below) return value(*above);
    if (!above || above->time <= below->time) return value(*below);
    return glm::mix(value(*below), value(*above), (t - below->time) / (above->time - below->time));
}

/**
 * @brief One key of a Curve: its value at a time between 0 and 1.
 */
struct CurveKey {
    float time  = 0.0f;
    float value = 1.0f;
};

/**
 * @brief A value over a span from 0 to 1 - a particle's life, say - straight between its keys.
 *
 * None reads 1; one reads its value throughout; before the first key and after the last the end
 * value holds.
 */
struct Curve {
    std::vector<CurveKey> keys;

    /**
     * @brief The value at @p t.
     *
     * @param t Where in the span, 0 to 1.
     * @return The value there.
     */
    float at(float t) const {
        return sampleKeys(keys, t, 1.0f, [](const CurveKey& key) { return key.value; });
    }
};

/**
 * @brief One key of a Gradient: its colour at a time between 0 and 1.
 */
struct GradientKey {
    float     time = 0.0f;
    glm::vec4 color{1.0f};
};

/**
 * @brief A colour over a span from 0 to 1, straight between its keys in linear RGBA.
 *
 * None reads white; one reads its colour throughout; before the first key and after the last the
 * end colour holds.
 */
struct Gradient {
    std::vector<GradientKey> keys;

    /**
     * @brief The colour at @p t.
     *
     * @param t Where in the span, 0 to 1.
     * @return The colour there.
     */
    glm::vec4 at(float t) const {
        return sampleKeys(keys, t, glm::vec4(1.0f), [](const GradientKey& key) { return key.color; });
    }
};

} // namespace Vkm::Engine::Math

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::FloatRange)
    VKM_F(min)
    VKM_F(max)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::CurveKey)
    VKM_F(time)
    VKM_F(value)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::Curve)
    VKM_F(keys)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::GradientKey)
    VKM_F(time)
    VKM_F(color)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::Gradient)
    VKM_F(keys)
VKM_REFLECT_END()
