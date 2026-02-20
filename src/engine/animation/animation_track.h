#pragma once

#include <algorithm>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "animation/easing.h"

namespace Engine {

/**
 * @brief AnimationTrack manages the interpolation of keyframe values of type T over time.
 *
 * This class is templated to support animation tracks of any GLM vector, scalar, or user type
 * where glm::mix is applicable. It stores a sorted list of keyframes (time, value pairs) and
 * provides value sampling (interpolation) at arbitrary time values using configurable easing functions.
 *
 * @tparam T The value type of the animation (e.g., glm::vec3, float).
 */
template<typename T>
class AnimationTrack {
    public:
        AnimationTrack(EasingFunction easing = Easing::linear) : m_easing(easing) {}
        ~AnimationTrack() = default;

        AnimationTrack(const AnimationTrack& other) = delete;
        AnimationTrack& operator=(const AnimationTrack& other) = delete;

        AnimationTrack(AnimationTrack && other) noexcept = default;
        AnimationTrack& operator=(AnimationTrack && other) noexcept = default;

    public:
        /**
         * @brief Adds a keyframe with a specified time and value.
         *        Keeps keyframes sorted by time ascending.
         * @param time The time (in seconds or arbitrary units).
         * @param value The value for the keyframe.
         */
        void addKeyframe(float time, const T& value) {
            // Find insertion point to maintain sorted order
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);
            auto index = static_cast<size_t>(it - m_times.begin());
            m_times.insert(it, time);
            m_values.insert(m_values.begin() + index, value);
        }

        /**
         * @brief Gets the interpolated value for a given time.
         *
         * If there are no keyframes, returns the default-constructed value.
         * If the track has only one keyframe, returns its value.
         * If time is before the first keyframe, returns the first value.
         * If time is after the last keyframe, returns the last value.
         * Otherwise, interpolates between nearest keyframes using the configured easing function.
         *
         * @param time The time (in seconds or arbitrary units).
         * @return The interpolated value at the given time.
         */
        T getValue(float time) const {
            if (m_times.empty()) {
                return T{};
            }

            if (m_times.size() == 1) {
                return m_values[0];
            }

            // Clamp time to track duration
            if (time < 0.0f) {
                return m_values[0];
            }

            if (time >= m_times.back()) {
                return m_values.back();
            }

            // Binary search on times only (cache-friendly: touches only floats)
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);

            size_t nextIndex = (it != m_times.end())
                ? static_cast<size_t>(it - m_times.begin())
                : m_times.size() - 1;

            size_t prevIndex = nextIndex - 1;

            // Calculate normalized time [0, 1] between the two keyframes
            float segmentDuration = m_times[nextIndex] - m_times[prevIndex];
            if (segmentDuration <= 0.0f) {
                return m_values[prevIndex];
            }

            float t = (time - m_times[prevIndex]) / segmentDuration;

            // Apply easing to the interpolation factor
            float eased = m_easing(t);

            // Interpolate using appropriate method (slerp for quaternions, mix for others)
            return interpolate(m_values[prevIndex], m_values[nextIndex], eased);
        }

        /**
         * @brief Gets the total duration of the track (the last keyframe's time, or 0.0 if empty).
         * @return The duration.
         */
        float getDuration() const {
            if (m_times.empty()) {
                return 0.0f;
            }
            return m_times.back();
        }

        /**
         * @brief Whether the track contains no keyframes.
         * @return True if empty, false otherwise.
         */
        bool isEmpty() const {
            return m_times.empty();
        }

        /**
         * @brief Set the interpolation easing function.
         * @param easing The easing function to use.
         */
        void setEasing(EasingFunction easing) {
            m_easing = easing;
        }

        /**
         * @brief Get the easing function currently used.
         * @return The easing function.
         */
        EasingFunction getEasing() const {
            return m_easing;
        }

        /**
         * @brief Removes all keyframes from the track.
         */
        void clear() {
            m_times.clear();
            m_values.clear();
        }

    private:
        /**
         * @brief Interpolates between two values using the appropriate method.
         * Uses spherical linear interpolation (slerp) for quaternions,
         * and linear interpolation (mix) for all other types.
         */
        template<typename U = T>
        static U interpolate(const U& a, const U& b, float t) {
            if constexpr (std::is_same_v<U, glm::quat>) {
                return glm::slerp(a, b, t);
            } else {
                return glm::mix(a, b, t);
            }
        }

        std::vector<float> m_times;   ///< Keyframe timestamps (sorted ascending)
        std::vector<T> m_values;       ///< Keyframe values (parallel to m_times)
        EasingFunction m_easing;
};

} // namespace Engine

