#pragma once

namespace Engine {

/**
 * @brief Converts real frame time into simulation time.
 *
 * The engine owns one clock and, each frame, turns the real frame delta into a
 * "simulation delta" via advance(): scaled while running, zero while paused, or
 * exactly N fixed steps' worth when single-stepping. That delta rides on
 * FrameContext::simDeltaTime, so update()-based simulation systems advance by it
 * (instead of the real delta) and the fixed-step accumulator fills from it -
 * pause, time-scale, and stepping all fall out of one number, with no
 * special-casing in the main loop. The editor drives the play state; the
 * runtime leaves the clock running at 1x.
 */
class SimulationClock {
    public:
        SimulationClock() = default;
        ~SimulationClock() = default;

        SimulationClock(const SimulationClock& other) = default;
        SimulationClock& operator=(const SimulationClock& other) = default;

        SimulationClock(SimulationClock && other) = default;
        SimulationClock& operator=(SimulationClock && other) = default;

    public:
        /**
         * @brief Pause or resume the clock.
         *
         * Changing the pause state discards any single-steps queued via
         * requestStep().
         *
         * @param paused True freezes simulation time, false resumes it.
         */
        void setPaused(bool paused) { m_paused = paused; m_pendingSteps = 0; }

        /**
         * @brief Slow-motion / fast-forward multiplier applied while running (clamped
         * to >= 0). Programmatic by design - no editor UI; drive it from a
         * script or console command.
         */
        void setTimeScale(float scale) { m_timeScale = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Queue fixed-step advances to play out while paused (editor "step").
         *
         * Non-positive counts are ignored. The queued steps are consumed by the
         * next advance() calls while the clock is paused.
         *
         * @param steps Number of fixed steps to enqueue (default 1).
         */
        void requestStep(int steps = 1) { if (steps > 0) m_pendingSteps += steps; }

        /**
         * @brief Simulation seconds elapsed this frame; consumes pending steps.
         *
         * Running: realDelta * timeScale. Paused with queued steps:
         * steps * fixedStep (then cleared). Paused and idle: 0.
         */
        float advance(float realDelta, float fixedStep) {
            if (!m_paused) {
                return realDelta * m_timeScale;
            }
            if (m_pendingSteps > 0) {
                const float stepped = static_cast<float>(m_pendingSteps) * fixedStep;
                m_pendingSteps = 0;
                return stepped;
            }
            return 0.0f;
        }

    public:
        bool isPaused() const { return m_paused; }
        float getTimeScale() const { return m_timeScale; }

    private:
        bool  m_paused       = false;
        int   m_pendingSteps = 0;
        float m_timeScale    = 1.0f;
};

} // namespace Engine
