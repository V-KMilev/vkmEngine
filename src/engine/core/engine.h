#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "platform/window/window_manager.h"
#include "debug/frame_tracker.h"
#include "core/system.h"
#include "core/simulation_clock.h"

namespace Engine {

/**
 * @brief Engine context: owns core state and runs the main loop.
 *
 * Owns the Scene, ResourceManager, WindowManager, FrameTracker, and the
 * per-stage system pipeline. Profiling is handled via debug/profiler.h
 * (Tracy facade) - the engine emits FrameMark per loop iteration and
 * per-stage CPU zones in updateStage(). GPU collect is the backend's job,
 * done at the tail of each RenderBackend::render() call.
 *
 * Non-copyable, non-movable, but stack-constructible: tests and
 * headless tooling can spin up their own Engine.
 *
 * Usage:
 *   Engine::Engine engine;
 *   engine.addSystem<CameraController>(SystemStage::Input);
 *   engine.addSystem<RenderSystem>(SystemStage::Render);
 *   engine.run();  // blocks until window closes
 */
class Engine {
    public:
        Engine();
        ~Engine();

        Engine(const Engine& other) = delete;
        Engine& operator=(const Engine& other) = delete;

        Engine(Engine && other) = delete;
        Engine& operator=(Engine && other) = delete;

    public:
        Scene& getScene()             { return m_scene; }
        const Scene& getScene() const { return m_scene; }

        ResourceManager& getResources()             { return m_resources; }
        const ResourceManager& getResources() const { return m_resources; }

        WindowManager& getWindow()             { return m_window; }
        const WindowManager& getWindow() const { return m_window; }

        /**
         * @brief Owns play/pause/step/time-scale for simulation.
         *
         * The main loop feeds its sim-delta to FrameContext; the editor drives
         * the play state. The runtime never touches it, so it simulates at 1x
         * from boot.
         */
        SimulationClock& getSimulationClock()             { return m_simClock; }
        const SimulationClock& getSimulationClock() const { return m_simClock; }

        /**
         * @brief Log "FPS: N (M ms)" to the console once a second.
         *
         * Opt-in and runtime-facing: the editor shows FPS in its status bar, so
         * it leaves this off to keep the console quiet.
         */
        void logFPS(bool enabled = true) { m_fpsLog = enabled; }

        /**
         * @brief Create and register a system at the given execution stage.
         *
         * Engine takes ownership. Stages run in SystemStage declaration order;
         * within a stage, systems run in registration order.
         *
         * @tparam T System subclass to create.
         * @tparam Args Constructor argument types.
         * @param stage Which frame stage this system belongs to.
         * @param args Forwarded to T's constructor.
         * @return Reference to the newly created system.
         */
        template<typename T, typename... Args>
        T& addSystem(SystemStage stage, Args&&... args) {
            auto system = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *system;
            m_systemsByStage[static_cast<size_t>(stage)].push_back(std::move(system));
            return ref;
        }

        /**
         * @brief Run the main loop (blocks until the window is closed).
         */
        void run();

    private:
        void initSystems(FrameContext& ctx);
        void shutdownSystems();

    private:
        Scene m_scene;
        ResourceManager m_resources;

        WindowManager m_window;
        FrameTracker  m_frameTracker;

        /// Systems organized by stage. Outer index is SystemStage; inner vector
        /// preserves registration order within that stage.
        std::array<std::vector<std::unique_ptr<System>>,
                   static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        /**
         * @brief Subset of systems that actually implement fixedUpdate().
         *
         * Built once at init from System::hasFixedUpdate(); the accumulator
         * loop iterates this list instead of dispatching the empty virtual
         * across every registered system every tick.
         */
        std::vector<System*> m_fixedUpdaters;

        bool m_initialized = false;

        SimulationClock m_simClock;

        /// Once-a-second FPS console log. Off by default (the editor uses its
        /// status bar); the runtime opts in. The timer throttles the log.
        bool  m_fpsLog      = false;
        float m_fpsLogTimer = 0.0f;

        /// Throttle state for accumulator-clamp warnings (one per second).
        /// Per-instance so headless tools / tests with multiple Engine
        /// instances don't cross-suppress each other.
        std::chrono::steady_clock::time_point m_lastAccumClampWarn{};
        int m_accumClampSuppressed = 0;
};

} // namespace Engine
