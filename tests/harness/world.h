#pragma once

// Two ways to stand the engine up, by the level a test needs:
// - Game: a headless Engine as a host stands one up (setupEngineApp), every system in its place,
//   run frame by frame on a pinned clock. Most tests use it: it is how a game runs.
// - Frame: the services one system is handed, for a test of that system alone.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "app/engine_app.h"
#include "base/event/event_bus.h"
#include "core/clock.h"
#include "core/engine.h"
#include "core/frame/render_readiness.h"
#include "core/frame_context.h"
#include "core/graphics.h"
#include "core/host_chrome.h"
#include "core/input_map.h"
#include "ecs/component/render/camera.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "vkm_platform/window/window_manager.h"
#include "resource/resource_manager.h"

#include "harness/scratch.h"

namespace Vkm::Test {

using namespace Vkm::Engine;

/**
 * @brief A headless engine with the standard system stack, its clock driven by the test.
 *
 * The world is paused and stepped: each frame runs exactly the ticks asked for and lasts exactly
 * their time, whatever the frame took, so a run is the same on any machine. No window and no
 * frame cap. Engine::run is called once, by run().
 */
class Game {
    public:
        explicit Game(uint32_t tickRate = 60);
        ~Game();

        Game(const Game& other) = delete;
        Game& operator=(const Game& other) = delete;

        Game(Game && other) = delete;
        Game& operator=(Game && other) = delete;

    public:
        /**
         * @brief Run @p frames frames of @p steps ticks each; Engine::run returns after the last.
         *
         * @param frames How many frames.
         * @param each   Called after every frame, last in it, with the frame's number from 1.
         * @param steps  Ticks a frame; 0 runs frames on a world standing still.
         */
        void run(int frames, const std::function<void(int frame)>& each = {}, int steps = 1);

        Scene&           scene()     { return engine.getScene(); }
        ResourceManager& resources() { return engine.getResources(); }
        Clock&           clock()     { return engine.getClock(); }
        EventBus&        events()    { return engine.getEvents(); }

        /// Host a game on @p port, as vkm_server does: the actions are this game's InputMap's.
        bool host(uint16_t port, uint32_t maxPlayers, uint32_t tickRate) {
            const InputMap& input = engine.getInput();
            return engine.getNet().host(
                port,
                maxPlayers,
                tickRate,
                input.actionCount(),
                input.actionFingerprint()
            );
        }

        /// Join the game at @p server, as vkm_runtime does: the actions are this game's InputMap's.
        bool join(const Network::Address& server, uint32_t tickRate) {
            const InputMap& input = engine.getInput();
            return engine.getNet().connect(server, tickRate, input.actionCount(), input.actionFingerprint());
        }

    public:
        Vkm::Engine::Engine engine;
        App::AppSystems     systems;
};

/**
 * @brief The services a FrameContext refers to, for a test of one system on a frame it builds.
 */
struct Frame {
    EventBus                events;  // before the scene, which a behavior unsubscribes from on its way out
    Scene                   scene;
    ResourceManager         resources;
    Clock                   clock;
    Platform::WindowManager window;  // never given a window
    InputMap                input;
    NetSession              net;
    HostChrome              chrome;
    Graphics                graphics;
    ActiveCamera            camera;
    RenderReadiness         readiness;
    FrameContext    ctx{FrameProducts{}, scene, resources, clock, events, window,
                        input, net, chrome, graphics, camera, readiness};

    /// Run @p system's fixedUpdate @p ticks times at the clock's step, flushing events before each.
    void tick(System& system, int ticks = 1);
};

/**
 * @brief Make @p name, under the test's scratch directory, the open project's root.
 *
 * @param name The project folder's name.
 * @return Its path; it exists, with an empty `scenes` and `prefabs`.
 */
std::filesystem::path openProject(const std::string& name = "project");

/// The engine's own tree, where the examples and the shaders are: Roots::engineRoot, as the engine finds it.
std::filesystem::path engineDir();

} // namespace Vkm::Test
