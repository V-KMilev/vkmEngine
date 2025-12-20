#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

/**
 * @brief Represents a single keyframe in an animation track.
 *
 * A keyframe holds a time (in seconds or arbitrary units) and a value of type T 
 * (such as position, rotation, or scale data). Keyframes are used to store the 
 * state of an animating property at discrete moments in time and are 
 * interpolated between to compute values at frames between keyframes.
 *
 * @tparam T The value type (for example, glm::vec3 for position/scale, glm::quat for rotation).
 */
template<typename T>
struct Keyframe {
    public:
        Keyframe() = delete;
        ~Keyframe() = default;

        Keyframe(const Keyframe& other) = delete;
        Keyframe& operator=(const Keyframe& other) = delete;

        Keyframe(Keyframe && other) noexcept = default;
        Keyframe& operator=(Keyframe && other) noexcept = default;

        /**
        * @brief Parameterized constructor.
        * @param t The keyframe time.
        * @param v The keyframe value.
        */
        Keyframe(float t, const T& v) : time(t), value(v) {}

    public:
        /**
        * @brief Compares keyframes by their time value for sorting.
        * @param other The other keyframe to compare.
        * @return True if this keyframe's time is less than the other's time.
        */
        bool operator<(const Keyframe& other) const {
            return time < other.time;
        }

    public:
        float time;
        T value;
};

/**
 * @brief Keyframe for position animation (glm::vec3).
 */
using PositionKeyframe = Keyframe<glm::vec3>;

/**
 * @brief Keyframe for rotation animation (glm::quat).
 */
using RotationKeyframe = Keyframe<glm::quat>;

/**
 * @brief Keyframe for scale animation (glm::vec3).
 */
using ScaleKeyframe = Keyframe<glm::vec3>;

} // namespace Engine

