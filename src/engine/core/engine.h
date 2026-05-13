#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "platform/window/window_manager.h"
#include "debug/statistics.h"
#include "core/system.h"

namespace Engine {

/**
 * @brief Central engine singleton that owns core state and runs the main loop.
 *
 * Owns the Scene, ResourceManager, and system pipeline. Systems are registered
 * in execution order and updated sequentially each frame via FrameContext.
 *
 * Usage:
 *   auto& engine = Engine::get();
 *   engine.addSystem(&cameraController);
 *   engine.addSystem(&renderManager);
 *   engine.run();
 */
class Engine {
    public:
        /**
         * @brief Get the singleton instance.
         * @return Reference to the Engine singleton.
         */
        static Engine& get();

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

        StatisticTracker& getStatistics()             { return m_statistics; }
        const StatisticTracker& getStatistics() const { return m_statistics; }

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
        Engine() = default;
        ~Engine() = default;

        void initSystems(FrameContext& ctx);
        void shutdownSystems();
        void printStats(const FrameContext& ctx);

    private:
        Scene m_scene;
        ResourceManager m_resources;

        WindowManager m_window;
        StatisticTracker m_statistics;

        /// Systems organized by stage. Outer index is SystemStage; inner vector
        /// preserves registration order within that stage.
        std::array<std::vector<std::unique_ptr<System>>,
                   static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        bool m_initialized = false;
};

} // namespace Engine
