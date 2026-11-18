#pragma once

#include <algorithm>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Whether a pass laid out in the eye's view is worked out this frame, or its last result is
 *        read again.
 *
 * While the eye holds still a pass is worked out at most MAX_RATE times a second: what moves in the
 * world moves too little in a frame to tell, but a turning eye's would lag it by pixels at every edge,
 * so a frame the eye moves always works it out. GTAO and the fog each keep one, the fog's half a draw
 * out of step, so the two fall on different frames and the cost stays even.
 */
class GLRateCap {
    public:
        static constexpr float MAX_RATE = 200.0f;  ///< Times a second at most it is worked out, eye still.

    public:
        /**
         * @brief A cap that starts @p phase of a draw owed.
         *
         * @param phase What is owed after a frame the eye moved, in [0, 1): two passes whose phases are
         *              half a draw apart are worked out on different frames.
         */
        explicit GLRateCap(float phase) : m_phase(phase), m_owed(phase) {}
        ~GLRateCap() = default;

        GLRateCap(const GLRateCap& other) = default;
        GLRateCap& operator=(const GLRateCap& other) = default;

        GLRateCap(GLRateCap && other) = default;
        GLRateCap& operator=(GLRateCap && other) = default;

    public:
        /**
         * @brief Whether the pass is worked out this frame.
         *
         * @param eye     The frame's world to clip.
         * @param seconds The frame's length; a frame of none always works it out.
         * @param held    Whether the pass holds a result it may read again.
         * @return True to work it out; false to read the last result again.
         */
        bool due(const glm::mat4& eye, float seconds, bool held) {
            const bool still = eye == m_eye;
            m_eye  = eye;
            m_owed = std::min(m_owed + seconds * MAX_RATE, 2.0f);
            if (held && still && m_owed < 1.0f && seconds > 0.0f) return false;
            m_owed = still ? std::max(m_owed - 1.0f, 0.0f) : m_phase;
            return true;
        }

    private:
        float     m_phase;      ///< Of a draw, owed after a frame the eye moved.
        float     m_owed;       ///< Draws the seconds gone have asked for, up to two.
        glm::mat4 m_eye{1.0f};  ///< The last frame's eye, world to clip.
};

} // namespace Vkm::Engine
