#include "net/wire/quantize.h"

#include <cmath>

namespace Vkm::Engine::Quantize {

namespace {

/**
 * @brief Bits one smallest-three component takes.
 *
 * Eleven holds a rotation to about a fifteenth of a degree at the worst
 * orientation. Nine was enough to look right on a body and was not: the error
 * is an angle, so what it costs grows with the thing being turned, and a
 * quarter of a degree across a sixty-metre floor moves its surface ten
 * centimetres. Two more bits per component is six bits a body.
 */
constexpr uint32_t ROTATION_BITS = 11;

/**
 * @brief A quaternion component's range is +/- 1/sqrt(2) once the largest is removed,
 * because if it were larger it would be the largest.
 */
constexpr float ROTATION_RANGE = 0.70710678f;

constexpr uint32_t VELOCITY_BITS = 16;

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
 * One fewer than half the codes, so the levels are symmetric about a middle one
 * that is exactly zero. Spreading the codes evenly across the range instead
 * leaves the midpoint between two of them, and then zero - by far the most
 * common value a rotation or a velocity takes - is the one value that cannot be
 * said.
 */
constexpr uint32_t signedLevels(uint32_t width) {
    return (1u << (width - 1u)) - 1u;
}

/**
 * @brief Write @p value in [-@p range, @p range], with zero exact.
 *
 * The alternative, and what this replaced, put zero half a step from the
 * nearest code. For a rotation that is a permanent tilt of about a quarter of a
 * degree on everything a client is told about - unnoticeable on a crate and
 * ten centimetres of vertical error at the far corner of a sixty-metre floor,
 * which a character then falls through. For a velocity it is a body at rest
 * that reports drift, so a settled world never goes quiet.
 */
uint32_t toSigned(float value, float range, uint32_t width) {
    const float    levels  = static_cast<float>(signedLevels(width));
    const float    clamped = glm::clamp(value / range, -1.0f, 1.0f);
    const int32_t  step    = static_cast<int32_t>(std::lround(clamped * levels));
    return static_cast<uint32_t>(step + static_cast<int32_t>(signedLevels(width)));
}

/// Read back what toSigned wrote.
float fromSigned(uint32_t raw, float range, uint32_t width) {
    const int32_t step = static_cast<int32_t>(raw) - static_cast<int32_t>(signedLevels(width));
    return (static_cast<float>(step) / static_cast<float>(signedLevels(width))) * range;
}

} // namespace

void writePosition(BitWriter& out, float value, float extent) {
    out.bits(toFixed(value, -extent, POSITION_STEP, positionBits(extent)), positionBits(extent));
}

float readPosition(BitReader& in, float extent) {
    return fromFixed(in.bits(positionBits(extent)), -extent, POSITION_STEP);
}

void writeRotation(BitWriter& out, const glm::quat& value) {
    const glm::quat q = glm::normalize(value);
    const float parts[4] = { q.x, q.y, q.z, q.w };

    uint32_t largest = 0;
    for (uint32_t i = 1; i < 4; ++i) {
        if (std::fabs(parts[i]) > std::fabs(parts[largest])) largest = i;
    }

    // q and -q name the same rotation, so the largest is written positive and
    // its sign never has to travel.
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

    // The one that was left out, recovered from the unit length it must have.
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
