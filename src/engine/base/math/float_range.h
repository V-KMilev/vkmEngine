#pragma once

#include "base/reflect/reflect.h"
#include "base/math/random.h"

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

} // namespace Vkm::Engine::Math

VKM_REFLECT_BEGIN(::Vkm::Engine::Math::FloatRange)
    VKM_F(min)
    VKM_F(max)
VKM_REFLECT_END()
