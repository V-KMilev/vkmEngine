#pragma once

namespace Vkm::Platform {
class WindowManager;
} // namespace Vkm::Platform

namespace Vkm::Engine {

class Clock;
struct HostChrome;

class Scene;
class ResourceManager;
class EventBus;
class InputMap;
class NetSession;
class PoseBuffer;
class Graphics;
class ActiveCamera;
struct HostView;
struct Visibility;
struct UIDrawData;
struct SplashFrame;
struct LoadingFrame;
struct LiveParticles;
struct TerrainFrame;
struct RenderReadiness;
class DebugDraw;

/**
 * @brief What the frame's stages publish for the stages after them: each null until its producer
 *        has run this frame, so a consumer is registered after its producer (in setupEngineApp).
 *
 * A null `hostView` means render through the scene's active camera.
 */
struct FrameProducts {
    const HostView*      hostView   = nullptr;  ///< From CameraControllerSystem.
    const Visibility*    visibility = nullptr;  ///< From VisibilitySystem.
    const PoseBuffer*    poses      = nullptr;  ///< From SkeletalAnimationSystem.
    const UIDrawData*    ui         = nullptr;  ///< From UISystem.
    const SplashFrame*   splash     = nullptr;  ///< From SplashSystem.
    const LoadingFrame*  loading    = nullptr;  ///< From LoadingSystem.
    const UIDrawData*    cover      = nullptr;  ///< From UISystem: the loading screen's UI, under its bar.
    const LiveParticles* particles  = nullptr;  ///< From ParticleSystem.
    const TerrainFrame*  terrain    = nullptr;  ///< From TerrainSystem.
};

/**
 * @brief The state every system is passed, for the whole session: the Engine's services, and the
 *        products of the frame running now.
 *
 * References are engine-owned services. The products are cleared at the top of every frame
 * (clearProducts), so one its producer did not publish this frame reads null, not the last
 * frame's - but for `debug`, a service a host may not keep.
 *
 * Simulation state reads getSimDelta() and getFixedStep(); presentation and
 * services run every frame regardless, so a paused game keeps its music and menu.
 */
struct FrameContext : FrameProducts {
    Scene&           scene;
    ResourceManager& resources;

    Clock&                   clock;
    EventBus&                events;
    Platform::WindowManager& window;
    InputMap&                input;
    NetSession&              net;
    HostChrome&              chrome;
    Graphics&                graphics;   ///< The session's render settings: the project's, and a game's
                                         ///< changes.
    ActiveCamera&            camera;     ///< Which camera the frame looks through; every reader asks it,
                                         ///< so all agree.
    RenderReadiness&         readiness;  ///< How far the backend's bakes are: RenderSystem writes it once
                                         ///< it has drawn, so a system of an earlier stage reads the last
                                         ///< frame's.

    DebugDraw* debug = nullptr;  ///< The host's world drawing, outliving the frame: any system, and the
                                 ///< threads its parallelFor forks, draw into it, and RenderSystem hands
                                 ///< the backend what it holds. Null when the host shows no frames, and
                                 ///< in a shipped game (VKM_SHIPPING).

    /// Forget the last frame's products, every one at once, as the frame begins.
    void clearProducts() { static_cast<FrameProducts&>(*this) = FrameProducts{}; }
};

} // namespace Vkm::Engine
