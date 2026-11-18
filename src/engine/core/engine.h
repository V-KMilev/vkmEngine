#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "vkm_platform/window/window_manager.h"
#include "core/system.h"
#include "frame/render_readiness.h"
#include "core/frame/tick_hold.h"
#include "core/clock.h"
#include "base/event/event_bus.h"
#include "core/graphics.h"
#include "core/host_chrome.h"
#include "ecs/component/render/camera.h"
#include "debug/draw/debug_draw.h"
#include "core/input_map.h"
#include "net/net_session.h"
#include "system/physics/drawn_ahead.h"

namespace Vkm::Engine {

/**
 * @brief Engine context: owns core state and runs the main loop.
 *
 * Owns the services handed to systems on FrameContext, and the per-stage system
 * pipeline. Stack-constructible: each host builds its own, as the tests' harness does.
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

        Platform::WindowManager& getWindow()             { return m_window; }
        const Platform::WindowManager& getWindow() const { return m_window; }

        /**
         * @brief What an authoring host says about the frame it draws over.
         *
         * For the host that writes it; systems read it off the frame context.
         */
        HostChrome& getChrome()             { return m_chrome; }
        const HostChrome& getChrome() const { return m_chrome; }

        /**
         * @brief The render settings this session draws with: the project's, and a game's changes.
         *
         * The host gives it the project's (Graphics::setProject); systems and behaviors use the
         * frame context.
         */
        Graphics& getGraphics()             { return m_graphics; }
        const Graphics& getGraphics() const { return m_graphics; }

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
        EventBus m_events;  ///< Declared before the scene so it outlives it: a behavior the scene destroys
                            ///< unsubscribes from it, even on a throw out of run().

        Scene           m_scene;
        ResourceManager m_resources;

        Clock                   m_clock;
        InputMap                m_input;
        Platform::WindowManager m_window;
        NetSession              m_net;
        HostChrome              m_chrome;
        Graphics                m_graphics;
        ActiveCamera            m_camera;      ///< FrameContext::camera, and the eye the commands steer by.
        RenderReadiness         m_readiness;   ///< FrameContext::readiness: written by one frame, read by
                                               ///< the next.
        TickHold                m_tickHold;    ///< Holds m_clock while a mesh its colliders or water need
                                               ///< decodes.
        DebugDraw               m_debug;       ///< FrameContext::debug while frames show; a shipped
                                               ///< game's never.
        DrawnAhead              m_drawnAhead;  ///< Moving bodies as each frame draws them.

        FrameContext m_frame;  ///< The services above and this frame's products, handed to every system for
                               ///< the session.

        std::array<SystemList, static_cast<size_t>(SystemStage::Count)> m_systemsByStage;

        bool m_initialized = false;
};

} // namespace Vkm::Engine
