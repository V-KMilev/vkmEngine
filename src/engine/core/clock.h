#pragma once

#include <chrono>
#include <cstdint>

#include "core/engine_config.h"

namespace Vkm::Engine {

/**
 * @brief The engine's frame clock: real + simulation time and the fixed-step accumulator.
 *
 * The engine owns one and calls beginFrame() at the top of each iteration. That
 * samples the real (wall-clock) delta and tops the fixed-step accumulator up
 * with it - scaled while running, nothing at all while paused. Steps asked for
 * through requestStep() are held apart from it as a count rather than folded
 * in as a span, which is what makes them exact at every tick rate. Systems read
 * getDeltaTime() for real-time work (input / camera / UI), getSimDelta() for
 * simulation update() and getFixedStep() in fixedUpdate(). Pause, time-scale
 * and single-step all fall out of those two with no special-casing in the loop.
 * The editor drives the play state (setPaused / requestStep); the runtime
 * leaves the clock at 1x.
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
         * @brief Take one fixed step if one is owed; the main-loop fixedUpdate condition.
         *
         * A step that was asked for is taken first and exactly, then time that
         * was measured. Which of the two it came from is not a caller's
         * business, but that the count is drained first is: it is what keeps a
         * commanded step from being rounded away at a rate whose step is not a
         * binary fraction.
         *
         * @return True having taken a step, false once neither is owed. Use as
         *         the condition of a `while (clock.consumeFixedStep()) { ... }`
         *         loop.
         */
        bool consumeFixedStep();

        float getDeltaTime() const { return m_deltaTime; }
        float getSimDelta() const  { return m_simDelta; }
        float getFixedStep() const { return m_fixedStep; }
        float getFrameRate() const { return m_deltaTime > 0.0f ? 1.0f / m_deltaTime : 0.0f; }
        float getFrameTime() const { return m_deltaTime * 1000.0f; }

        bool  isPaused() const     { return m_paused; }
        float getTimeScale() const { return m_timeScale; }

        /**
         * @brief Set the simulation cadence, in ticks per second.
         *
         * The host sets this when it reads a project, not per frame: every
         * fixedUpdate in the engine is written against a step that does not
         * change under it.
         *
         * @param ticksPerSecond Clamped to [Config::MIN_TICK_RATE,
         *        Config::MAX_TICK_RATE].
         */
        void setTickRate(uint32_t ticksPerSecond) {
            m_fixedStep = 1.0f / static_cast<float>(Config::clampTickRate(ticksPerSecond));
        }

        /**
         * @brief Pause or resume simulation time.
         *
         * Queued steps survive it. A step that was asked for is a tick that was
         * asked for, and which way the play state happened to move afterwards
         * is not a reason to lose it.
         *
         * @param paused True freezes simulation time (sim delta 0); false resumes it.
         */
        void setPaused(bool paused) { m_paused = paused; }

        /**
         * @brief Set the slow-motion / fast-forward multiplier applied while running.
         *
         * Clamped to >= 0. Programmatic by design (no editor UI) - drive it from a
         * script or console command. Because no UI can undo it, the editor puts
         * the scale back to 1 when a play session ends, so a scale a behavior
         * set belongs to that session only.
         *
         * @param scale Time-scale multiplier; negative values are clamped to 0.
         */
        void setTimeScale(float scale) { m_timeScale = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Queue fixed-step advances, whatever the play state.
         *
         * Delivered as a count rather than as a span of seconds, and that is the
         * whole of why it is separate from the accumulator: a fixed step is
         * rarely a binary fraction, so asking for n and converting to n * step
         * and back loses ticks at most rates - twenty asked at sixty hertz
         * arrives as nineteen. Draining an integer is exact at every rate.
         *
         * Non-positive counts are ignored.
         *
         * @param steps Number of fixed steps to enqueue (default 1).
         */
        void requestStep(int steps = 1) { if (steps > 0) m_pendingSteps += steps; }

        /**
         * @brief Which fixed step is running, counting from one.
         *
         * Zero until the first has run, so a reader can tell "before any tick"
         * from "during the first". It advances if and only if consumeFixedStep()
         * hands one out, which is what keeps it from being a second counter that
         * can disagree with the loop it describes.
         *
         * @return The tick number.
         */
        uint32_t getTick() const { return m_tick; }

    private:
        std::chrono::steady_clock::time_point m_last{};
        bool  m_started = false;

        float m_fixedStep = 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE);

        float m_deltaTime   = 0.0f;
        float m_simDelta    = 0.0f;
        float m_accumulator = 0.0f;

        bool  m_paused       = false;
        int   m_pendingSteps = 0;
        float m_timeScale    = 1.0f;

        /**
         * @brief Fixed steps handed out this session.
         *
         * Advanced only by the one function that hands them out, so it cannot describe
         * a loop it is out of step with.
         */
        uint32_t m_tick = 0;
};

} // namespace Vkm::Engine
