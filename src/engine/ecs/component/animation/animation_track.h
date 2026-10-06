#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/math/easing.h"

namespace Vkm::Engine {

/**
 * @brief Keyframes of type T sorted by time, sampled through an easing curve.
 *
 * T is any type glm::mix accepts.
 *
 * @tparam T Value type, e.g. glm::vec3 or float.
 */
template<typename T>
class AnimationTrack {
    public:
        /// Two keyframe times closer than this, in seconds, are one instant.
        static constexpr float SAME_INSTANT_SECONDS = 1e-4f;

    public:
        AnimationTrack() = default;

        /**
         * @brief Construct a track that interpolates with @p easing.
         *
         * @param easing The curve; see setEasing.
         */
        explicit AnimationTrack(Easing easing) : m_easing(easing) {}

        ~AnimationTrack() = default;

        AnimationTrack(const AnimationTrack& other) = default;
        AnimationTrack& operator=(const AnimationTrack& other) = default;

        AnimationTrack(AnimationTrack && other) noexcept = default;
        AnimationTrack& operator=(AnimationTrack && other) noexcept = default;

    public:
        /**
         * @brief Add a keyframe at @p time.
         *
         * A keyframe at a time already held is added beside it, not replacing it;
         * setKeyframe() re-keys.
         *
         * @param time Time in the unit the track is read at, usually seconds.
         * @param value Value at that time.
         * @return Index the sort put it at.
         */
        size_t addKeyframe(float time, const T& value) {
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);
            auto index = static_cast<size_t>(it - m_times.begin());
            m_times.insert(it, time);
            m_values.insert(m_values.begin() + index, value);
            return index;
        }

        /**
         * @brief Set the value at @p time, replacing any keyframe already there.
         *
         * Matches approximately, so re-keying an instant leaves no zero-length segment.
         *
         * @param time Time to key at.
         * @param value Value at that time.
         */
        void setKeyframe(float time, const T& value) {
            for (size_t i = 0; i < m_times.size(); ++i) {
                if (std::fabs(m_times[i] - time) < SAME_INSTANT_SECONDS) {
                    m_values[i] = value;
                    return;
                }
            }
            addKeyframe(time, value);
        }

        /**
         * @brief Remove the keyframe at @p index; out of range is a no-op.
         *
         * @param index Keyframe to remove.
         */
        void removeKeyframe(size_t index) {
            if (index >= m_times.size()) return;
            m_times.erase(m_times.begin() + static_cast<std::ptrdiff_t>(index));
            m_values.erase(m_values.begin() + static_cast<std::ptrdiff_t>(index));
        }

        /**
         * @brief Replace the value of the keyframe at @p index; out of range is a no-op.
         *
         * @param index Keyframe to change.
         * @param value New value.
         */
        void setKeyframeValue(size_t index, const T& value) {
            if (index >= m_values.size()) return;
            m_values[index] = value;
        }

        /**
         * @brief Move the keyframe at @p index to @p time.
         *
         * The track stays sorted, so the keyframe's index may change.
         *
         * @param index Keyframe to move; out of range is a no-op.
         * @param time Time to move it to.
         * @return Its new index, or @p index when there was nothing to move.
         */
        size_t setKeyframeTime(size_t index, float time) {
            if (index >= m_times.size()) return index;
            T value = m_values[index];
            removeKeyframe(index);
            return addKeyframe(time, value);
        }

        void clear() {
            m_times.clear();
            m_values.clear();
        }

        /**
         * @brief Choose the curve this track eases its interpolation with.
         *
         * @param easing The curve, such as Easing::EaseInOutSine.
         */
        void setEasing(Easing easing) {
            m_easing = easing;
        }

        /**
         * @brief Sample the track at @p time.
         *
         * An empty track returns T{}; outside the keyframe range the nearest end value holds.
         *
         * @param time Time in the track's unit.
         * @return The eased, interpolated value.
         */
        T getValue(float time) const {
            if (m_times.empty()) {
                return T{};
            }

            if (m_times.size() == 1) {
                return m_values[0];
            }

            // Otherwise a first key past 0 reaches upper_bound == begin() and underflows prevIndex.
            if (time <= m_times.front()) {
                return m_values.front();
            }

            if (time >= m_times.back()) {
                return m_values.back();
            }

            // time < m_times.back() here, so upper_bound never returns end().
            auto it = std::upper_bound(m_times.begin(), m_times.end(), time);

            const size_t nextIndex = static_cast<size_t>(it - m_times.begin());
            const size_t prevIndex = nextIndex - 1;

            float segmentDuration = m_times[nextIndex] - m_times[prevIndex];
            if (segmentDuration <= 0.0f) {
                return m_values[prevIndex];
            }

            float t = (time - m_times[prevIndex]) / segmentDuration;

            const float eased = easingFunction(m_easing)(t);

            return interpolate(m_values[prevIndex], m_values[nextIndex], eased);
        }

        /**
         * @brief The track's length.
         *
         * @return The last keyframe's time, or 0 for an empty track.
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

        size_t keyframeCount() const {
            return m_times.size();
        }

        /**
         * @brief The curve this track eases with.
         *
         * @return The curve.
         */
        Easing getEasing() const {
            return m_easing;
        }

        /**
         * @brief The keyframe times, ascending.
         *
         * @return One time per keyframe.
         */
        const std::vector<float>& getTimes() const { return m_times; }

        /**
         * @brief The keyframe values, parallel to getTimes.
         *
         * @return One value per keyframe.
         */
        const std::vector<T>& getValues() const { return m_values; }

    private:
        /**
         * @brief Slerp for quaternions (short arc, constant speed), linear for every other type.
         *
         * @tparam U Value type; defaults to T.
         * @param a Value at the segment's start.
         * @param b Value at the segment's end.
         * @param t Eased position, 0 to 1; outside that where the easing overshoots.
         * @return The value at @p t.
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
        std::vector<float> m_times;   ///< Sorted ascending
        std::vector<T>     m_values;  ///< Parallel to m_times
        Easing             m_easing = Easing::Linear;
};

} // namespace Vkm::Engine

