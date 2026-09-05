#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/math/easing.h"

namespace Vkm::Engine {

/**
 * @brief AnimationTrack manages the interpolation of keyframe values of type T over time.
 *
 * Works for any GLM vector, scalar, or user type where glm::mix is applicable.
 * Keyframes are stored sorted by time and sampled through a configurable easing
 * function.
 *
 * @tparam T The value type of the animation (e.g., glm::vec3, float).
 */
template<typename T>
class AnimationTrack {
    public:
        AnimationTrack() = default;
        ~AnimationTrack() = default;

        AnimationTrack(const AnimationTrack& other) = default;
        AnimationTrack& operator=(const AnimationTrack& other) = default;

        AnimationTrack(AnimationTrack && other) noexcept = default;
        AnimationTrack& operator=(AnimationTrack && other) noexcept = default;

        /**
         * @brief Construct a track that interpolates with @p easing.
         *
         * The default constructor uses linear, per m_easing's initializer.
         */
        explicit AnimationTrack(EasingFunction easing) : m_easing(easing) {}

    public:
        /**
         * @brief Add a keyframe at @p time.
         *
         * Keeps the keyframes sorted by ascending time. A second keyframe at a
         * time already held is added beside the first rather than replacing it;
         * setKeyframe() is the call that re-keys.
         *
         * @param time Time in seconds, or in whatever unit the track is read at.
         * @param value Value the track takes at that time.
         */
        void addKeyframe(float time, const T& value) {
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);
            auto index = static_cast<size_t>(it - m_times.begin());
            m_times.insert(it, time);
            m_values.insert(m_values.begin() + index, value);
        }

        /**
         * @brief Set the value at @p time, replacing any keyframe already there.
         *
         * Matches an existing keyframe approximately rather than exactly, which
         * is what keeps re-keying at the same instant from leaving a
         * zero-length segment behind.
         *
         * @param time Time to key at.
         * @param value Value the track takes at that time.
         */
        void setKeyframe(float time, const T& value) {
            for (size_t i = 0; i < m_times.size(); ++i) {
                if (std::fabs(m_times[i] - time) < 1e-4f) {
                    m_values[i] = value;
                    return;
                }
            }
            addKeyframe(time, value);
        }

        /**
         * @brief Gets the interpolated value for a given time.
         *
         * An empty track returns the default-constructed value; a time outside the
         * keyframe range holds the nearest end value.
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

            // Hold the first value at or before the first key. `time < 0.0f` is
            // not enough: a first key past 0 would reach the interpolation with
            // upper_bound == begin(), underflowing prevIndex to SIZE_MAX.
            if (time <= m_times.front()) {
                return m_values.front();
            }

            if (time >= m_times.back()) {
                return m_values.back();
            }

            // Binary search on times only (cache-friendly: touches only floats).
            // time < m_times.back() here (the >= case returned above), so
            // upper_bound always lands on an element - never end().
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);

            const size_t nextIndex = static_cast<size_t>(it - m_times.begin());
            const size_t prevIndex = nextIndex - 1;

            float segmentDuration = m_times[nextIndex] - m_times[prevIndex];
            if (segmentDuration <= 0.0f) {
                return m_values[prevIndex];
            }

            float t = (time - m_times[prevIndex]) / segmentDuration;

            float eased = m_easing(t);

            return interpolate(m_values[prevIndex], m_values[nextIndex], eased);
        }

        /**
         * @brief Gets the total duration of the track (the last keyframe's time, or 0.0 if empty).
         */
        float getDuration() const {
            if (m_times.empty()) {
                return 0.0f;
            }
            return m_times.back();
        }

        bool isEmpty() const {
            return m_times.empty();
        }

        void setEasing(EasingFunction easing) {
            m_easing = easing;
        }

        EasingFunction getEasing() const {
            return m_easing;
        }

        void clear() {
            m_times.clear();
            m_values.clear();
        }

        size_t keyframeCount() const {
            return m_times.size();
        }

        /**
         * @brief Remove the keyframe at @p index.
         *
         * A no-op if @p index is out of range.
         */
        void removeKeyframe(size_t index) {
            if (index >= m_times.size()) return;
            m_times.erase(m_times.begin() + static_cast<std::ptrdiff_t>(index));
            m_values.erase(m_values.begin() + static_cast<std::ptrdiff_t>(index));
        }

        /**
         * @brief Replace the value of the keyframe at @p index.
         *
         * A no-op if @p index is out of range.
         */
        void setKeyframeValue(size_t index, const T& value) {
            if (index >= m_values.size()) return;
            m_values[index] = value;
        }

        /**
         * @brief Move the keyframe at @p index to @p time.
         *
         * The track stays sorted, so the keyframe may come to sit at a
         * different index than the one named here. A no-op if @p index is out
         * of range.
         */
        void setKeyframeTime(size_t index, float time) {
            if (index >= m_times.size()) return;
            T value = m_values[index];
            removeKeyframe(index);
            addKeyframe(time, value);
        }

        /**
         * @brief Read-only access to keyframe storage - used by serialization and
         * any tool that needs to round-trip the track's contents.
         */
        const std::vector<float>& getTimes()  const { return m_times; }
        const std::vector<T>&     getValues() const { return m_values; }

    private:
        /**
         * @brief Interpolate between two values by the method the type wants.
         *
         * Spherical linear interpolation for quaternions, so a rotation takes
         * the short arc at constant speed; plain linear interpolation for
         * every other type.
         */
        template<typename U = T>
        static U interpolate(const U& a, const U& b, float t) {
            if constexpr (std::is_same_v<U, glm::quat>) {
                return glm::slerp(a, b, t);
            } else {
                return glm::mix(a, b, t);
            }
        }

    private:
        std::vector<float> m_times;   ///< Keyframe timestamps (sorted ascending)
        std::vector<T>     m_values;  ///< Keyframe values (parallel to m_times)
        EasingFunction     m_easing = Easing::linear;
};

} // namespace Vkm::Engine

