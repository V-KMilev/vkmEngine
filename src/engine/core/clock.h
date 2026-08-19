#pragma once

#include <chrono>

#include "core/engine_config.h"

namespace Vkm::Engine {

/**
 * @brief The engine's frame clock: real + simulation time and the fixed-step accumulator.
 *
 * The engine owns one and calls beginFrame() at the top of each iteration. That
 * samples the real (wall-clock) delta, converts it into a simulation delta -
 * scaled while running, zero while paused, exactly N fixed steps' worth when
 * single-stepping - and tops up the fixed-step accumulator from that sim delta.
 * Systems read getDeltaTime() for real-time work (input / camera / UI),
 * getSimDelta() for simulation update() and getFixedStep() in fixedUpdate().
 * Pause, time-scale, and single-step all fall out of the one sim delta with no
 * special-casing in the loop. The editor drives the play state (setPaused /
 * requestStep); the runtime leaves the clock at 1x.
 */
class Clock {
    public:
        Clock() = default;
        ~Clock() = default;

        Clock(const Clock& other) = default;
        Clock& operator=(const Clock& other) = default;

        Clock(Clock && other) = default;
        Clock& operator=(Clock && other) = default;

    public:
        /**
         * @brief Open a new frame: measure the real delta, derive the sim delta, fill the accumulator.
         *
         * Call once at the top of the main loop, before reading any of the values
         * below. Uses a monotonic steady clock; the first call reports a zero delta
         * so a slow startup never produces a giant opening step.
         */
        void beginFrame();

        /**
         * @brief Consume one fixed step from the accumulator; the main-loop fixedUpdate condition.
         *
         * @return True while at least one whole fixed step remains this frame (and
         *         decrements the accumulator by one step); false once the frame's
         *         fixed budget is spent. Use as the condition of a
         *         `while (clock.consumeFixedStep()) { ... }` loop.
         */
        bool consumeFixedStep();

        float getDeltaTime() const { return m_deltaTime; }
        float getSimDelta() const  { return m_simDelta; }
        float getFixedStep() const { return Config::FIXED_TIME_STEP; }
        float getFrameRate() const { return m_deltaTime > 0.0f ? 1.0f / m_deltaTime : 0.0f; }
        float getFrameTime() const { return m_deltaTime * 1000.0f; }

        bool  isPaused() const     { return m_paused; }
        float getTimeScale() const { return m_timeScale; }

        /**
         * @brief Pause or resume simulation time.
         *
         * Changing the pause state discards any single-steps queued via requestStep().
         *
         * @param paused True freezes simulation time (sim delta 0); false resumes it.
         */
        void setPaused(bool paused) {
            m_paused = paused;
            m_pendingSteps = 0;
        }

        /**
         * @brief Set the slow-motion / fast-forward multiplier applied while running.
         *
         * Clamped to >= 0. Programmatic by design (no editor UI) - drive it from a
         * script or console command.
         *
         * @param scale Time-scale multiplier; negative values are clamped to 0.
         */
        void setTimeScale(float scale) { m_timeScale = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Queue fixed-step advances to play out while paused (the editor "step").
         *
         * Non-positive counts are ignored; queued steps are consumed by the next
         * beginFrame() while paused, that one frame feeding them all at once.
         *
         * @param steps Number of fixed steps to enqueue (default 1).
         */
        void requestStep(int steps = 1) { if (steps > 0) m_pendingSteps += steps; }

    private:
        float simDeltaFor(float realDelta);

    private:
        std::chrono::steady_clock::time_point m_last{};
        bool  m_started = false;

        float m_deltaTime   = 0.0f;
        float m_simDelta    = 0.0f;
        float m_accumulator = 0.0f;

        bool  m_paused       = false;
        int   m_pendingSteps = 0;
        float m_timeScale    = 1.0f;
};

} // namespace Vkm::Engine
