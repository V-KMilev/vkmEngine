#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "platform/window/window_manager.h"
#include "core/system.h"
#include "system/async/async_loader_system.h"
#include "system/render/render_backend.h"
#include "system/render/render_settings.h"
#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/host_chrome.h"
#include "debug/debug_draw.h"
#include "platform/input/input_map.h"
#include "net/net_session.h"

namespace Vkm::Engine {

/**
 * @brief Engine context: owns core state and runs the main loop.
 *
 * Owns the services handed to systems on FrameContext, and the per-stage system
 * pipeline. Stack-constructible, so tests and headless tooling can make their own.
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
        /**
         * @brief Log "FPS: N (M ms)" to the console once a second.
         *
         * @param enabled False to stop.
         */
        void setFPSLog(bool enabled = true) { m_fpsLog = enabled; }

        /**
         * @brief Run the main loop, blocking until the window closes or the process is interrupted.
         */
        void run();

        /**
         * @brief Create and register a system at the given execution stage.
         *
         * Stages run in SystemStage order; within one, in registration order.
         *
         * @tparam T System subclass to create.
         * @tparam Args Constructor argument types.
         * @param stage Frame stage it runs in.
         * @param args Forwarded to T's constructor.
         * @return The new system, owned by Engine.
         */
        template<typename T, typename... Args>
        T& addSystem(SystemStage stage, Args&&... args) {
            auto system = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *system;
            m_systemsByStage[static_cast<size_t>(stage)].push_back(std::move(system));
            return ref;
        }

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
         * For the host; systems and behaviors use the frame context.
         */
        InputMap& getInput()             { return m_input; }
        const InputMap& getInput() const { return m_input; }

        WindowManager& getWindow()             { return m_window; }
        const WindowManager& getWindow() const { return m_window; }

        /**
         * @brief What an authoring host says about the frame it draws over.
         *
         * For the host that writes it; systems read it off the frame context.
         */
        HostChrome& getChrome()             { return m_chrome; }
        const HostChrome& getChrome() const { return m_chrome; }

        /**
         * @brief The quality settings this session draws at.
         *
         * The project.json render block, after a player's changes; writable
         * while the game runs.
         */
        RenderSettings& getRenderSettings()             { return m_render; }
        const RenderSettings& getRenderSettings() const { return m_render; }

        /**
         * @brief Start the session from @p project, the project.json render block: the session's
         *        settings and the copy gameplay builds a quality tier over (withPreset).
         *
         * @param project The block, as loadProject read it.
         */
        void setProjectRenderSettings(const RenderSettings& project) {
            m_projectRender = project;
            m_render        = project;
        }

        /// The project.json render block the session started from (setProjectRenderSettings).
        const RenderSettings& getProjectRenderSettings() const { return m_projectRender; }

        /**
         * @brief The session this end is playing in, offline until told otherwise.
         *
         * A host starts a game through NetSession::host or NetSession::connect;
         * gameplay reaches it through FrameContext.
         */
        NetSession& getNet()             { return m_net; }
        const NetSession& getNet() const { return m_net; }

    private:
        using SystemList = std::vector<std::unique_ptr<System>>;

        void initSystems(FrameContext& ctx);
        void shutdownSystems();

    private:
        /// Declared before the scene so it outlives it: a behavior the scene
        /// destroys unsubscribes from it, even on a throw out of run().
        EventBus m_events;

        Scene m_scene;
        ResourceManager m_resources;

        Clock           m_clock;
        InputMap        m_input;
        WindowManager   m_window;
        NetSession      m_net;
        HostChrome      m_chrome;
        RenderSettings  m_render;
        RenderSettings  m_projectRender;
        RenderReadiness m_readiness;  ///< FrameContext::readiness: written by one frame, read by the next.
        ArrivalHold     m_arrival;    ///< Holds m_clock while an arriving world's meshes decode.
        DebugDraw       m_debug;      ///< FrameContext::debug while frames show; a shipped game's never.

        std::array<SystemList, static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        bool m_initialized = false;
        bool m_fpsLog      = false;
};

} // namespace Vkm::Engine
