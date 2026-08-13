#pragma once

#include <array>
#include <memory>
#include <vector>

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "platform/window/window_manager.h"
#include "core/system.h"
#include "core/clock.h"
#include "core/event/event_bus.h"
#include "platform/input/input_map.h"

namespace Engine {

/**
 * @brief Engine context: owns core state and runs the main loop.
 *
 * Owns the Scene, ResourceManager, WindowManager, Clock, the EventBus
 * (gameplay pub/sub - infrastructure like the Clock; the loop flushes its
 * queue at the top of the Simulation stage), and the per-stage system
 * pipeline. Profiling is handled via debug/profiler.h
 * (Tracy facade) - the engine emits FrameMark per loop iteration and
 * a per-stage CPU zone over each stage's update(). GPU collect is the backend's job,
 * done at the tail of each RenderBackend::render() call.
 *
 * Non-copyable, non-movable, but stack-constructible: tests and
 * headless tooling can spin up their own Engine.
 *
 * Usage:
 *   Engine::Engine engine;
 *   engine.addSystem<CameraControllerSystem>(SystemStage::Input);
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

        Clock& getClock()             { return m_clock; }
        const Clock& getClock() const { return m_clock; }

        EventBus& getEvents()             { return m_events; }
        const EventBus& getEvents() const { return m_events; }

        /**
         * @brief The action map gameplay reads input through.
         *
         * Exposed so the bootstrap can install a project's bindings and a
         * controls screen can edit them; systems and behaviors reach it through
         * the frame context instead.
         */
        InputMap& getInput()             { return m_input; }
        const InputMap& getInput() const { return m_input; }

        WindowManager& getWindow()             { return m_window; }
        const WindowManager& getWindow() const { return m_window; }

        /**
         * @brief Log "FPS: N (M ms)" to the console once a second.
         *
         * Opt-in and runtime-facing: the editor shows FPS in its status bar, so
         * it leaves this off to keep the console quiet.
         */
        void setFPSLog(bool enabled = true) { m_fpsLog = enabled; }

        /**
         * @brief Run the main loop (blocks until the window is closed).
         */
        void run();

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

    private:
        void initSystems(FrameContext& ctx);
        void shutdownSystems();

    private:
        Scene m_scene;
        ResourceManager m_resources;

        Clock         m_clock;
        EventBus      m_events;
        InputMap      m_input;
        WindowManager m_window;

        std::array<std::vector<std::unique_ptr<System>>, static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        bool m_initialized = false;
        bool m_fpsLog      = false;
};

} // namespace Engine
