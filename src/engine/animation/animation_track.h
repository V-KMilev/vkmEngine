#pragma once

#include <algorithm>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "animation/keyframe.h"
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
            m_keyframes.emplace_back(time, value);

            // Keep keyframes sorted by time
            std::sort(m_keyframes.begin(), m_keyframes.end());
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
            if (m_keyframes.empty()) {
                // Default value
                return T{};
            }

            if (m_keyframes.size() == 1) {
                return m_keyframes[0].value;
            }

            // Clamp time to track duration
            float duration = getDuration();
            if (time < 0.0f) {
                return m_keyframes[0].value;
            }

            if (time >= duration) {
                return m_keyframes.back().value;
            }

            // Binary search for the first keyframe with time > query time
            auto it = std::upper_bound(
                m_keyframes.begin(), m_keyframes.end(), time,
                [](float t, const Keyframe<T>& kf) { return t < kf.time; }
            );

            size_t nextIndex = (it != m_keyframes.end())
                ? static_cast<size_t>(it - m_keyframes.begin())
                : m_keyframes.size() - 1;

            size_t prevIndex = nextIndex - 1;
            const auto& prev = m_keyframes[prevIndex];
            const auto& next = m_keyframes[nextIndex];

            // Calculate normalized time [0, 1] between the two keyframes
            float segmentDuration = next.time - prev.time;
            if (segmentDuration <= 0.0f) {
                return prev.value;
            }

            float t = (time - prev.time) / segmentDuration;

            // Apply easing to the interpolation factor
            float eased = m_easing(t);

            // Interpolate using appropriate method (slerp for quaternions, mix for others)
            return interpolate(prev.value, next.value, eased);
        }

        /**
         * @brief Gets the total duration of the track (the last keyframe's time, or 0.0 if empty).
         * @return The duration.
         */
        float getDuration() const {
            if (m_keyframes.empty()) {
                return 0.0f;
            }
            return m_keyframes.back().time;
        }

        /**
         * @brief Whether the track contains no keyframes.
         * @return True if empty, false otherwise.
         */
        bool isEmpty() const {
            return m_keyframes.empty();
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
            m_keyframes.clear();
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

        std::vector<Keyframe<T>> m_keyframes;
        EasingFunction m_easing;
};

} // namespace Engine

