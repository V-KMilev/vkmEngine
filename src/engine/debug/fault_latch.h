#pragma once

namespace Vkm::Engine {

/**
 * @brief Names a recurring fault once per gap rather than once a frame.
 *
 * A pass logs when report() answers true, and calls endPass() however it exits.
 * A fault that clears and returns is named again.
 */
class FaultLatch {
    public:
        FaultLatch() = default;
        ~FaultLatch() = default;

        FaultLatch(const FaultLatch& other) = default;
        FaultLatch& operator=(const FaultLatch& other) = default;

        FaultLatch(FaultLatch && other) = default;
        FaultLatch& operator=(FaultLatch && other) = default;

        /**
         * @brief Record that the pass in progress met the fault.
         *
         * @return True, and the caller logs, when the fault is unnamed since a
         *         pass last missed it.
         */
        bool report() {
            m_seen = true;
            if (m_named) return false;
            m_named = true;
            return true;
        }

        /**
         * @brief Close the pass: a fault it did not meet is named again next time.
         */
        void endPass() {
            m_named = m_seen;
            m_seen  = false;
        }

    private:
        bool m_seen  = false;  ///< Met during the pass in progress.
        bool m_named = false;  ///< Logged since a whole pass last missed it.
};

} // namespace Vkm::Engine
