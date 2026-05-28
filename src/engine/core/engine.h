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
         * @brief Move the Render + UI stages and swapBuffers onto a
         *        dedicated GL thread.
         *
         * Must be called before run() to take effect. Once enabled the
         * GL context migrates off the main thread after engine boot
         * completes; only the render thread may issue GL calls thereafter.
         *
         * Sequential mode today: main thread waits for the render thread
         * each frame, so no FPS win - the win is the architectural
         * isolation. ImGui-using builds (the editor) should leave this
         * OFF until the ImGui draw-data handoff is wired up.
         */
        void enableRenderThread(bool enable) { m_renderThreadEnabled = enable; }

        /**
         * @brief Run the main loop (blocks until the window is closed).
         */
        void run();

        /**
         * @brief Allow systems within @p stage to dispatch in parallel
         *        across layers of the per-stage schedule.
         *
         * STATUS: API present, not yet wired anywhere. vkmEngine's stages
         * currently have <=2 systems each, so layer partitioning buys
         * nothing - the per-system data-parallel `parallelFor` inside
         * HierarchySystem / AnimationSystem / VisibilitySystem is the
         * actual scaling lever today. Revisit once 3+ peers share a stage
         * (Physics + AI + Particles + Audio under Simulation is the
         * intended motivating case).
         *
         * Off by default for every stage. Systems within a stage are
         * grouped into "layers" by their SystemAccess declarations - two
         * systems share a layer only when neither's writes overlap the
         * other's reads or writes. With parallel dispatch on, the ThreadPool
         * fans out across each layer's systems and waits for the layer to
         * finish before moving on; without it, layers run sequentially in
         * registration order.
         *
         * The caller is responsible for guaranteeing the systems in @p stage
         * are thread-safe at the level of their declared accesses. The
         * audit checklist:
         *   - No scene.add / scene.remove / scene.destroyEntity (structural
         *     SparseSet mutations are not thread-safe).
         *   - No shared external writes outside declared component access
         *     (ResourceManager, GPU state, file system, ...).
         *   - No add-on-read patterns (e.g. HierarchyOperations adds a
         *     WorldTransform if missing - that's a structural change). The
         *     hierarchy-attach prereq fix has eliminated this one specifically,
         *     but the audit shape stays.
         *   - SystemAccess::reads/writes only describe component access; any
         *     system that reads ResourceManager, GPU state, or other external
         *     state from inside its update body cannot be declared parallel-
         *     safe by an empty SystemAccess - it should stay conservative
         *     (default-empty, treated as "writes everything").
         */
        void setParallelDispatch(SystemStage stage, bool enabled);

    private:
        /**
         * @brief Per-system bundle: the system pointer + its declared access.
         */
        struct ScheduledSystem {
            System*       system = nullptr;
            SystemAccess  access;
        };

        /**
         * @brief Per-stage execution plan computed once at init time.
         *
         * Systems within a stage are partitioned into layers using a greedy
         * assignment: each system goes into the earliest layer where its
         * reads/writes don't conflict with any system already in that layer.
         * Layers run in order; within a layer, systems run concurrently
         * when parallel dispatch is on, sequentially otherwise.
         */
        struct StageSchedule {
            std::vector<std::vector<ScheduledSystem>> layers;
            bool parallelDispatch = false;
        };

        void initSystems(FrameContext& ctx);
        void shutdownSystems();
        void buildSchedule();   ///< Compute m_schedule from m_systemsByStage.
        void updateStage(SystemStage stage, FrameContext& ctx);

    private:
        Scene m_scene;
        ResourceManager m_resources;

        WindowManager m_window;
        FrameTracker  m_frameTracker;

        /// Systems organized by stage. Outer index is SystemStage; inner vector
        /// preserves registration order within that stage.
        std::array<std::vector<std::unique_ptr<System>>,
                   static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        std::array<StageSchedule,
                   static_cast<size_t>(SystemStage::Count)> m_schedule;

        /**
         * @brief Subset of systems that actually implement fixedUpdate(). Built
         *
         * once at init from System::hasFixedUpdate(); the accumulator loop
         * iterates this list instead of dispatching the empty virtual
         * across every registered system every tick.
         */
        std::vector<System*> m_fixedUpdaters;

        bool m_initialized = false;

        /// Set via enableRenderThread() before run(); honoured at the
        /// first frame after engine boot. Default false keeps the
        /// existing single-threaded behaviour for the editor binary.
        bool m_renderThreadEnabled = false;

        /// Lazily constructed in run() once boot is complete. Owns the
        /// GL context for its lifetime; destroyed at run() exit.
        std::unique_ptr<RenderThread> m_renderThread;

        /// Cached pointer to the registered RenderSystem (if any). Used
        /// by the overlap loop to call buildView() on main + executeFrame()
        /// on the render thread independently. Filled lazily on the first
        /// frame after initSystems; nullptr until then.
        RenderSystem* m_renderSystem = nullptr;

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
