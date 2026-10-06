#include "net/wire/quantize.h"

#include <algorithm>
#include <cmath>

namespace Vkm::Engine::Quantize {

namespace {

/**
 * @brief Range of a quaternion component other than the largest: +/- 1/sqrt(2).
 */
constexpr float ROTATION_RANGE = 0.70710678f;

uint32_t toFixed(float value, float low, float step, uint32_t width) {
    const float  offset = (value - low) / step;
    const double top    = static_cast<double>((1ull << width) - 1ull);
    if (offset <= 0.0f) return 0;
    if (static_cast<double>(offset) >= top) return static_cast<uint32_t>(top);
    return static_cast<uint32_t>(offset + 0.5f);
}

float fromFixed(uint32_t raw, float low, float step) {
    return low + static_cast<float>(raw) * step;
}

/**
 * @brief Steps either side of zero for a signed field @p width bits wide.
 *
 * One fewer than half the codes, so a middle code is exactly zero.
 *
 * @param width Bits.
 * @return Steps on each side of the zero code.
 */
constexpr int32_t signedLevels(uint32_t width) {
    return static_cast<int32_t>((1u << (width - 1u)) - 1u);
}

} // namespace

uint32_t toSigned(float value, float range, uint32_t width) {
    const int32_t levels = signedLevels(width);
    if (std::isnan(value)) return static_cast<uint32_t>(levels);
    const float   clamped = glm::clamp(value / range, -1.0f, 1.0f);
    const int32_t step    = static_cast<int32_t>(std::lround(clamped * static_cast<float>(levels)));
    return static_cast<uint32_t>(step + levels);
}

float fromSigned(uint32_t raw, float range, uint32_t width) {
    const int32_t levels = signedLevels(width);
    // The top code is one toSigned never writes; a damaged packet still can.
    const int32_t step = std::min(static_cast<int32_t>(raw) - levels, levels);
    return (static_cast<float>(step) / static_cast<float>(levels)) * range;
}

void writePosition(BitWriter& out, float value) {
    const float bounded = std::isfinite(value)
        ? std::min(std::max(value, -WORLD_EXTENT), WORLD_EXTENT)
        : 0.0f;
    out.bits(toFixed(bounded, -WORLD_EXTENT, POSITION_STEP, positionBits()), positionBits());
}

float readPosition(BitReader& in) {
    // A corrupt packet can carry codes past +WORLD_EXTENT.
    const float decoded = fromFixed(in.bits(positionBits()), -WORLD_EXTENT, POSITION_STEP);
    return std::min(std::max(decoded, -WORLD_EXTENT), WORLD_EXTENT);
}

void writeRotation(BitWriter& out, const glm::quat& value) {
    const glm::quat q = glm::normalize(value);
    const float parts[4] = { q.x, q.y, q.z, q.w };

    uint32_t largest = 0;
    for (uint32_t i = 1; i < 4; ++i) {
        if (std::fabs(parts[i]) > std::fabs(parts[largest])) largest = i;
    }

    // q and -q are the same rotation, so the largest's sign need not travel.
    const float flip = parts[largest] < 0.0f ? -1.0f : 1.0f;

    out.bits(largest, 2);
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == largest) continue;
        out.bits(toSigned(parts[i] * flip, ROTATION_RANGE, ROTATION_BITS), ROTATION_BITS);
    }
}

glm::quat readRotation(BitReader& in) {
    const uint32_t largest = in.bits(2);

    float parts[4];
    float sum = 0.0f;
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == largest) continue;
        parts[i] = fromSigned(in.bits(ROTATION_BITS), ROTATION_RANGE, ROTATION_BITS);
        sum += parts[i] * parts[i];
    }

    // The largest, from the unit length.
    parts[largest] = std::sqrt(sum < 1.0f ? 1.0f - sum : 0.0f);

    return glm::normalize(glm::quat(parts[3], parts[0], parts[1], parts[2]));
}

void writeVelocity(BitWriter& out, float value) {
    out.bits(toSigned(value, MAX_SPEED, VELOCITY_BITS), VELOCITY_BITS);
}

float readVelocity(BitReader& in) {
    return fromSigned(in.bits(VELOCITY_BITS), MAX_SPEED, VELOCITY_BITS);
}

} // namespace Vkm::Engine::Quantize
