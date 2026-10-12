#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Frames a moved or resized capture holds still before it bakes again.
 *
 * A bake is costly and a box being dragged changes every frame: until it settles, the last bake
 * stands.
 */
inline constexpr uint32_t CAPTURE_SETTLE_FRAMES = 8;

/**
 * @brief What a capture is asked for, and for how many frames running it has been the same.
 *
 * @tparam Ask Where and how the capture would be made, comparable for equality.
 */
template <typename Ask>
class CaptureSettle {
    public:
        CaptureSettle()  = default;
        ~CaptureSettle() = default;

        CaptureSettle(const CaptureSettle& other) = default;
        CaptureSettle& operator=(const CaptureSettle& other) = default;

        CaptureSettle(CaptureSettle && other) = default;
        CaptureSettle& operator=(CaptureSettle && other) = default;

    public:
        /**
         * @brief Note this frame's ask, counting the frames it has held.
         *
         * @param now What this frame asks for.
         */
        void see(const Ask& now) {
            if (now == m_last) {
                if (m_still < CAPTURE_SETTLE_FRAMES) ++m_still;
                return;
            }
            m_last  = now;
            m_still = 0;
        }

        /// Whether the ask has held CAPTURE_SETTLE_FRAMES frames.
        bool settled() const { return m_still >= CAPTURE_SETTLE_FRAMES; }

        /// The last ask seen.
        const Ask& last() const { return m_last; }

    private:
        Ask      m_last{};
        uint32_t m_still = 0;  ///< Frames m_last has held, up to CAPTURE_SETTLE_FRAMES.
};

} // namespace Vkm::Engine
