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

namespace Engine {

class RenderSystem;
class RenderThread;

/**
 * @brief Engine context: owns core state and runs the main loop.
 *
 * Owns the Scene, ResourceManager, WindowManager, FrameTracker, and the
 * per-stage system pipeline. Profiling is handled via debug/profiler.h
 * (Tracy facade) - the engine emits FrameMark per loop iteration and
 * per-stage CPU zones in updateStage(). GPU collect is the backend's job,
 * fired via RenderBackend::endFrame at the tail of RenderGraph::execute.
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
        // Default ctor + dtor out-of-line so the unique_ptr<RenderThread>'s
        // default deleter sees the full RenderThread type (forward-declared
        // at the top of this header).
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

        /**
         * @brief Owns the rendering backend's context for the duration
         *        of the main loop.
         *
         * Lazily constructed in run() once boot is complete (so the
         * single-thread bring-up phase can still touch the backend), and
         * destroyed at run() exit so any backend teardown in shutdown
         * finds the context on main again.
         */
        std::unique_ptr<RenderThread> m_renderThread;

        /**
         * @brief Cached pointer to the registered RenderSystem (if any).
         *
         * Used by the overlap loop to call buildView() on main +
         * executeFrame() on the render thread independently. Filled
         * lazily on the first frame after initSystems; nullptr until then.
         */
        RenderSystem* m_renderSystem = nullptr;

        /**
         * @brief Cached list of UI-stage systems that report
         *        hasBackendWork()==true.
         *
         * Their executeBackend() runs inside the render-thread lambda
         * after RenderSystem::executeFrame, so the UI lands on top of
         * the rendered scene. Filled lazily alongside m_renderSystem.
         */
        std::vector<System*> m_backendWorkSystems;

        /// Monotonic per-frame counter; drives RenderView buffer parity
        /// in the overlap loop (m_views[frameIndex & 1]). Incremented
        /// after each frame is posted.
        uint32_t m_renderFrameIndex = 0;

        /// Throttle state for accumulator-clamp warnings (one per second).
        /// Per-instance so headless tools / tests with multiple Engine
        /// instances don't cross-suppress each other.
        std::chrono::steady_clock::time_point m_lastAccumClampWarn{};
        int m_accumClampSuppressed = 0;
};

} // namespace Engine
