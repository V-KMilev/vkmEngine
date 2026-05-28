#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

/**
 * @brief A single keyframe in an animation track: time + value.
 *
 * @tparam T The value type (glm::vec3 for position/scale, glm::quat for rotation).
 */
template<typename T>
struct Keyframe {
    float time = 0.0f;
    T     value{};

    bool operator<(const Keyframe& other) const {
        return time < other.time;
    }
};

using PositionKeyframe = Keyframe<glm::vec3>;
using RotationKeyframe = Keyframe<glm::quat>;
using ScaleKeyframe    = Keyframe<glm::vec3>;

} // namespace Engine
