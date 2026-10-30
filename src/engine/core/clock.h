#pragma once

#include <chrono>
#include <cstdint>

#include <glm/glm.hpp>

#include "io/project/tick_rate.h"

namespace Vkm::Engine {

class NetSession;

namespace Config {

    // Caps the accumulator so a hitch cannot queue ticks faster than frames run
    // ("spiral of death"). A higher rate gets more ticks per hitch, not a longer stall.
    constexpr float MAX_FRAME_ACCUMULATOR = 0.25f;

    static_assert(
        1.0f / static_cast<float>(MIN_TICK_RATE) <= MAX_FRAME_ACCUMULATOR,
        "A step longer than the accumulator cap can never be consumed, "
        "so the slowest allowed tick rate would never tick at all"
    );

} // namespace Config

/**
 * @brief The engine's frame clock: real + simulation time and the fixed-step accumulator.
 *
 * Systems read getDeltaTime() for real-time work (input, camera, UI),
 * getSimDelta() for simulation update() and getFixedStep() in fixedUpdate().
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
         * The first call reports zero, so a slow startup gives no giant step. A
         * frame longer than Config::MAX_FRAME_ACCUMULATOR adds only that much sim
         * time, so a hitch's excess is never simulated; getDeltaTime still reports
         * the whole frame. A frame stepped while paused (requestStep) lasts its
         * steps, not the wall time: what moves by real time moves with the world, as
         * at that frame rate, however long the frame takes to draw.
         */
        void beginFrame();

        /**
         * @brief Take one fixed step if one is owed; the main-loop fixedUpdate condition.
         *
         * Requested steps are taken first, then measured time.
         *
         * @return True having taken a step, false once none is owed.
         */
        bool consumeFixedStep();

        /**
         * @brief Set the simulation cadence, in ticks per second.
         *
         * Set when a project is read, never per frame: fixedUpdate assumes a fixed step.
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
         * Queued steps survive it.
         *
         * @param paused True freezes simulation time (sim delta 0).
         */
        void setPaused(bool paused) { m_paused = paused; }

        /**
         * @brief Stop simulation time while a menu that pauses the game shows (UICanvas::pausesGame).
         *
         * UISystem's own, apart from the pause a host or game sets, so closing the menu never resumes
         * a pause the editor made. Queued steps survive it, as they survive a pause.
         *
         * @param shown True freezes simulation time (sim delta 0).
         */
        void setMenuShown(bool shown) { m_menuShown = shown; }

        /**
         * @brief Hold or release simulation time for a world waiting to arrive whole.
         *
         * Engine::run's own, apart from the play state a host or game pauses: while held no
         * time passes and no step is taken, a queued one included, which waits for the release.
         * Taking hold drops the simulation time the frame and the accumulator already hold, so
         * the release owes no burst of steps. Letting go gives the frame the steps queued
         * meanwhile, which it runs: its update and contacts see them.
         *
         * @param held True freezes simulation time.
         */
        void setHeld(bool held) {
            if (held && !m_held) {
                m_simDelta    = 0.0f;
                m_accumulator = 0.0f;
            }
            if (!held && m_held) m_simDelta = static_cast<float>(m_pendingSteps) * m_fixedStep;
            m_held = held;
        }

        /**
         * @brief Set the slow-motion / fast-forward multiplier applied while running.
         *
         * In the editor a scale lasts one play session; see
         * PlaySession::end.
         *
         * @param scale Negative is clamped to 0.
         */
        void setTimeScale(float scale) { m_timeScale = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Set how many fixed steps a second of wall time buys, as a multiple.
         *
         * A networked client runs slightly fast or slow so the server's command
         * queue neither runs dry nor backs up (NetPacing); the step never changes.
         * Multiplied with the time scale; Engine::run sets it each frame from
         * NetSession::pacing().
         *
         * @param scale Negative is clamped to 0.
         */
        void setPacing(float scale) { m_pacing = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Set the host's playback speed, multiplied with the game's own time scale.
         *
         * It runs a session slow or fast without touching what the game set; a runtime leaves it
         * at one.
         *
         * @param scale Negative is clamped to 0.
         */
        void setHostScale(float scale) { m_hostScale = scale < 0.0f ? 0.0f : scale; }

        /**
         * @brief Queue fixed-step advances, whatever the play state.
         *
         * A count, not seconds: a step is rarely a binary fraction, so n * step
         * converted back would lose ticks.
         *
         * @param steps Fixed steps to enqueue; non-positive is ignored.
         */
        void requestStep(int steps = 1) { if (steps > 0) m_pendingSteps += steps; }

        float getDeltaTime() const { return m_deltaTime; }
        float getSimDelta() const  { return m_simDelta; }
        float getFixedStep() const { return m_fixedStep; }

        /**
         * @brief How far into the next fixed step this frame falls, 0..1.
         *
         * The accumulator's leftover, as a fraction of a step past the last tick.
         */
        float getFixedAlpha() const {
            return m_fixedStep > 0.0f ? glm::clamp(m_accumulator / m_fixedStep, 0.0f, 1.0f) : 0.0f;
        }

        /**
         * @brief Frames per second, smoothed over roughly the last half second.
         *
         * For this frame alone, use getDeltaTime().
         *
         * @return Zero before the first frame.
         */
        float getFrameRate() const { return m_smoothedDelta > 0.0f ? 1.0f / m_smoothedDelta : 0.0f; }

        /**
         * @brief The smoothed frame as a duration rather than a rate.
         *
         * @return Milliseconds per frame; zero before the first frame.
         */
        float getFrameTime() const { return m_smoothedDelta * 1000.0f; }

        bool isPaused() const    { return m_paused; }
        bool isHeld() const      { return m_held; }
        bool isMenuShown() const { return m_menuShown; }

        /**
         * @brief Whether the world stands still this frame.
         *
         * True when no simulation time passes and no step is asked for, so no
         * tick can follow; Engine::run drops input latched on such a frame.
         */
        bool isSimulationStill() const { return m_simDelta <= 0.0f; }

        float getTimeScale() const { return m_timeScale; }
        float getHostScale() const { return m_hostScale; }

        /**
         * @brief Which fixed step is running, counting from one.
         *
         * Advances only when consumeFixedStep() hands one out.
         *
         * @return The tick number; zero before the first.
         */
        uint32_t getTick() const { return m_tick; }

    private:
        std::chrono::steady_clock::time_point m_last{};
        bool                                  m_started = false;

        float m_fixedStep = 1.0f / static_cast<float>(Config::DEFAULT_TICK_RATE);

        float m_deltaTime     = 0.0f;
        float m_simDelta      = 0.0f;
        float m_accumulator   = 0.0f;
        float m_smoothedDelta = 0.0f;  ///< m_deltaTime through a half-second lag.

        bool  m_paused       = false;
        bool  m_held         = false;  ///< See setHeld.
        bool  m_menuShown    = false;  ///< See setMenuShown.
        int   m_pendingSteps = 0;
        float m_timeScale    = 1.0f;
        float m_pacing       = 1.0f;
        float m_hostScale    = 1.0f;

        uint32_t m_tick = 0;
};

/**
 * @brief The authority's simulated time at @p clock's moment (NetSession::worldTime): its tick, how
 *        far past it the frame falls, and a tick's length.
 *
 * @param net   The session, which knows whose time it is.
 * @param clock The engine's clock.
 * @return Seconds of the authority's simulated time.
 */
double worldTime(const NetSession& net, const Clock& clock);

} // namespace Vkm::Engine
