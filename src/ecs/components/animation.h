#pragma once

#include <cstdint>
#include <memory>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "component.h"
#include "animation_track.h"

namespace Engine {

/**
 * @brief Component representing an animation that can be applied to an entity.
 * 
 * Supports animating position, rotation, and scale of Transform components.
 * The animation system will automatically apply these animations each frame.
 */
class Animation final : public Component {
    public:
        Animation() = delete;
        ~Animation() override = default;

        /**
         * @brief Construct an Animation component.
         * @param id Unique component identifier.
         */
        Animation(uint32_t id);

    public:
        /**
         * @brief Update the animation time by delta time.
         * @param deltaTime Time elapsed in seconds.
         */
         void update(float deltaTime);

         /**
         * @brief Get the position animation track.
         * @return Reference to the position track.
         */
        AnimationTrack<glm::vec3>& getPositionTrack() { return m_positionTrack; }
        const AnimationTrack<glm::vec3>& getPositionTrack() const { return m_positionTrack; }

        /**
         * @brief Get the rotation animation track.
         * @return Reference to the rotation track.
         */
        AnimationTrack<glm::quat>& getRotationTrack() { return m_rotationTrack; }
        const AnimationTrack<glm::quat>& getRotationTrack() const { return m_rotationTrack; }

        /**
         * @brief Get the scale animation track.
         * @return Reference to the scale track.
         */
        AnimationTrack<glm::vec3>& getScaleTrack() { return m_scaleTrack; }
        const AnimationTrack<glm::vec3>& getScaleTrack() const { return m_scaleTrack; }

        /**
         * @brief Get the current animation time.
         * @return Current time in seconds.
         */
        float getTime() const { return m_time; }

        /**
         * @brief Get the animation duration (longest track duration).
         * @return Duration in seconds.
         */
         float getDuration() const {
            return std::max({
                m_positionTrack.getDuration(),
                m_rotationTrack.getDuration(),
                m_scaleTrack.getDuration()
            });
        }

        /**
         * @brief Get the playback speed multiplier.
         * @return Speed (1.0 = normal, 2.0 = double speed, etc.).
         */
         float getSpeed() const { return m_speed; }

        /**
         * @brief Set the current animation time.
         * @param time Time in seconds.
         */
        void setTime(float time) { m_time = time; }

        /**
         * @brief Set whether the animation should loop.
         * @param looping True to loop.
         */
         void setLooping(bool looping) { m_looping = looping; }

         /**
          * @brief Set the playback speed multiplier.
          * @param speed Speed (1.0 = normal, 2.0 = double speed, etc.).
          */
         void setSpeed(float speed) { m_speed = speed; }

        /**
         * @brief Check if the animation is playing.
         * @return True if playing.
         */
        bool isPlaying() const { return m_playing; }

        /**
         * @brief Check if the animation should loop.
         * @return True if looping.
         */
         bool isLooping() const { return m_looping; }

        /**
         * @brief Start playing the animation.
         */
        void play() { m_playing = true; }

        /**
         * @brief Pause the animation.
         */
        void pause() { m_playing = false; }

        /**
         * @brief Stop the animation and reset time to 0.
         */
        void stop() {
            m_playing = false;
            m_time = 0.0f;
        }

    private:
        AnimationTrack<glm::vec3> m_positionTrack;
        AnimationTrack<glm::quat> m_rotationTrack;
        AnimationTrack<glm::vec3> m_scaleTrack;

        float m_time;
        float m_speed;
        bool m_playing;
        bool m_looping;
};

} // namespace Engine

