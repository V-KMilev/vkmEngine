#include "net/wire/quantize.h"

#include <algorithm>
#include <cmath>

namespace Vkm::Engine::Quantize {

void writePosition(Network::BitWriter& out, float value) {
    const float bounded = std::isfinite(value)
        ? std::min(std::max(value, -WORLD_EXTENT), WORLD_EXTENT)
        : 0.0f;
    out.bits(Network::toFixed(bounded, -WORLD_EXTENT, POSITION_STEP, positionBits()), positionBits());
}

float readPosition(Network::BitReader& in) {
    // A corrupt packet can carry codes past +WORLD_EXTENT.
    const float decoded = Network::fromFixed(in.bits(positionBits()), -WORLD_EXTENT, POSITION_STEP);
    return std::min(std::max(decoded, -WORLD_EXTENT), WORLD_EXTENT);
}

void writeRotation(Network::BitWriter& out, const glm::quat& value) {
    Network::writeRotation(out, value, ROTATION_BITS);
}

glm::quat readRotation(Network::BitReader& in) {
    return Network::readRotation(in, ROTATION_BITS);
}

void writeVelocity(Network::BitWriter& out, float value) {
    out.bits(Network::toSigned(value, MAX_SPEED, VELOCITY_BITS), VELOCITY_BITS);
}

float readVelocity(Network::BitReader& in) {
    return Network::fromSigned(in.bits(VELOCITY_BITS), MAX_SPEED, VELOCITY_BITS);
}

} // namespace Vkm::Engine::Quantize
