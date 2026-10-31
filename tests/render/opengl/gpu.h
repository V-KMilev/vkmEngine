#pragma once

// The GPU tests' world: the engine as a host stands it up, headless, drawing through GLBackend into an
// off-screen surface of this machine's GPU (egl_context.h); the frames it draws, read back as a
// screenshot reads them; and the reference pictures they are compared with.

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_platform/threading/thread_pool.h"

#include "egl_context.h"
#include "harness/world.h"

namespace Vkm::Engine {
class GLBackend;
}

namespace Vkm::OpenGL {
class ComputeShader;
}

namespace Vkm::Test {

inline constexpr uint32_t WIDTH  = 384;  ///< What every GPU test draws at: small, so a frame is quick, and as
                                         ///< wide as a screen.
inline constexpr uint32_t HEIGHT = 216;

/**
 * @brief A rect of a frame's pixels, from x0, y0 to x1, y1 exclusive; the default is the whole frame.
 */
struct Rect {
    uint32_t x0 = 0;
    uint32_t y0 = 0;
    uint32_t x1 = WIDTH;
    uint32_t y1 = HEIGHT;

    /// The square of half side @p radius about @p center, kept in the frame.
    static Rect around(const glm::vec2& center, int radius);
};

/**
 * @brief A frame as read back: 8-bit RGB, top row first.
 */
struct Picture {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> rgb;

    /// The pixel at @p x, @p y, each channel 0..255.
    glm::vec3 at(uint32_t x, uint32_t y) const;

    /// The mean color of @p rect.
    glm::vec3 mean(const Rect& rect = {}) const;
};

/// A color's brightness as the eye weighs it, in the channels' own scale.
inline float luminance(const glm::vec3& color) { return glm::dot(
    color,
    glm::vec3(0.2126f, 0.7152f, 0.0722f)
); }

/**
 * @brief The share of @p rect's pixels of which a channel is more than @p levels apart.
 *
 * @param a      One frame.
 * @param b      Another, the same size.
 * @param levels How far a channel may be off and the pixel still agree.
 * @param rect   Where.
 * @return 0 to 1; 1 for frames of different sizes.
 */
float differing(const Picture& a, const Picture& b, int levels = 8, const Rect& rect = {});

/// How far apart two frames are over @p rect: the mean of every channel's difference, in levels.
float meanApart(const Picture& a, const Picture& b, const Rect& rect = {});

/// The share of a frame's pixels that are not black.
float lit(const Picture& picture);

/**
 * @brief This machine's GPU with an off-screen surface WIDTH x HEIGHT, current on this thread.
 *
 * With none the test is skipped, saying why, unless VKM_GPU_REQUIRED=1 asks the machine for one:
 * then it fails.
 */
class Device {
    public:
        Device();

        /// GL_RENDERER: which GPU draws.
        const std::string& renderer() const { return m_context.renderer(); }

    private:
        GLContext m_context;
};

/**
 * @brief This machine's GPU with the renderer's shader prelude, for a fact stated in C++ and again in GLSL:
 *        a probe, a compute shader in tests/render/opengl/probes, works out the GLSL's side on the GPU.
 */
class Probes {
    public:
        Probes();
        ~Probes();

        Probes(const Probes& other) = delete;
        Probes& operator=(const Probes& other) = delete;

        Probes(Probes && other) = delete;
        Probes& operator=(Probes && other) = delete;

    public:
        /**
         * @brief Run probe @p name over @p rows and read them back.
         *
         * A probe takes 64 threads a group, a thread a row of the storage buffer at binding 0.
         *
         * @param name Its folder in tests/render/opengl/probes.
         * @param rows What it reads, a multiple of 64 of them.
         * @param set  Sets its uniforms, for a probe that has any.
         * @return The rows as it left them.
         */
        std::vector<glm::vec4> run(
            const std::string& name,
            const std::vector<glm::vec4>& rows,
            const std::function<void(const Vkm::OpenGL::ComputeShader&)>& set = {}
        ) const;

    private:
        Device m_device;
};

class OffscreenBackend;

/**
 * @brief A game drawn on the GPU: a headless engine with GLBackend behind it, its scene the size of
 *        the off-screen surface.
 */
class Screen {
    public:
        /// @param tickRate Ticks a second of the world, which a frame's steps last.
        explicit Screen(uint32_t tickRate = 60);
        ~Screen();

        Screen(const Screen& other) = delete;
        Screen& operator=(const Screen& other) = delete;

        Screen(Screen && other) = delete;
        Screen& operator=(Screen && other) = delete;

    public:
        /**
         * @brief Run the world frozen until the renderer is ready, then @p frames frames of @p steps
         *        ticks each.
         *
         * Ready is every program compiled, every bake done and the ident's images made, held for a few
         * frames. Engine::run returns after the last frame, so a test runs once.
         *
         * @param frames How many frames after it is ready.
         * @param each   Called after each of them is drawn, with its number from 1.
         * @param steps  Ticks a frame.
         */
        void play(int frames, const std::function<void(int frame)>& each = {}, int steps = 1);

        /**
         * @brief Play past the engine's ident and take the frame: what a player sees once the world
         *        shows.
         *
         * @param before Called after each frame, with its number from 1, before the last.
         * @return The last frame.
         */
        Picture shot(const std::function<void(int frame)>& before = {});

        /**
         * @brief Play past the ident and take the frame; then, for each of @p changes, make it, play
         *        @p settle frames and take the frame again.
         *
         * @param changes What to change before each frame after the first.
         * @param settle  Frames a change is given before its frame is taken.
         * @return The frames, the first before any change.
         */
        std::vector<Picture> shots(const std::vector<std::function<void()>>& changes, int settle = 4);

        /// The frame just drawn, read back from the surface as a screenshot is; kept in the test's
        /// directory as frame_<n>.png, for a failure to show.
        Picture picture() const;

        /// The renderer's readiness, as its last frame left it.
        RenderReadiness readiness() const;

        Scene&           scene()     { return game.scene(); }
        ResourceManager& resources() { return game.resources(); }
        Graphics&        graphics()  { return game.engine.getGraphics(); }
        HostChrome&      chrome()    { return game.engine.getChrome(); }
        RenderBackend&   backend();

        /// The OpenGL backend behind backend(), for what it shows only to tests.
        const GLBackend& gl() const;

    public:
        Device device;  // first: the game's backend draws on it and goes before it
        Game   game;

    private:
        OffscreenBackend* m_backend = nullptr;
        mutable int       m_taken   = 0;  ///< Frames picture() has kept.
};

/**
 * @brief Every worker of the pool held in a task of its own until let go, as long reads hold them while
 *        a world loads: what is queued meanwhile runs only on a thread that waits for it.
 */
class HeldWorkers {
    public:
        HeldWorkers() {
            Platform::ThreadPool& pool = Platform::ThreadPool::get();
            for (size_t i = 0; i < pool.threadCount(); ++i) {
                pool.addTask([state = m_state] {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    ++state->held;
                    state->changed.notify_all();
                    state->changed.wait(lock, [&] { return state->open; });
                });
            }
            std::unique_lock<std::mutex> lock(m_state->mutex);
            m_state->changed.wait(lock, [&] { return m_state->held == pool.threadCount(); });
        }

        /// Let them go, and return once each has run what was queued while they were held: a task is
        /// taken only once every batch is, so with a task on every worker at once nothing else runs.
        void release() {
            Platform::ThreadPool& pool = Platform::ThreadPool::get();
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                m_state->open = true;
            }
            m_state->changed.notify_all();
            for (size_t i = 0; i < pool.threadCount(); ++i) {
                pool.addTask([state = m_state, workers = pool.threadCount()] {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    ++state->drained;
                    state->changed.notify_all();
                    state->changed.wait(lock, [&] { return state->drained == workers; });
                });
            }
            std::unique_lock<std::mutex> lock(m_state->mutex);
            m_state->changed.wait(lock, [&] { return m_state->drained == pool.threadCount(); });
        }

    private:
        struct State {
            std::mutex              mutex;
            std::condition_variable changed;
            size_t                  held    = 0;
            size_t                  drained = 0;
            bool                    open    = false;
        };

        std::shared_ptr<State> m_state = std::make_shared<State>();
};

inline constexpr int PAST_THE_IDENT = 44;  ///< Frames of 16 ticks that take a world past the engine's ident:
                                           ///< its clock steps a quarter second a frame at most.

/// Whether this run is a fresh start that freshStart() asked for: it draws its scene's last state
/// from nothing, keeps the frame (keepFreshStart) and ends.
bool isFreshStart();

/// In a fresh start, keep @p frame for the run that asked for it.
void keepFreshStart(const Picture& frame);

/**
 * @brief Run @p test again, in a process of its own, as a fresh start; take the frame it kept.
 *
 * What the engine draws of a scene with nothing carried from a run before it: what a frame after a
 * walk and its changes must be.
 *
 * @param test The test's name, without its area.
 * @return The frame, or an empty one, said, when the run kept none.
 */
Picture freshStart(const std::string& test);

/**
 * @brief Compare @p seen with the reference picture @p name, made on this GPU.
 *
 * tests/render/opengl/pictures/<name>.png, made by VKM_UPDATE_PICTURES=1 on the renderer
 * pictures/renderer.txt names; on another GPU the test is skipped. A pixel differs when a channel is more
 * than 8 levels apart, the picture when over 0.1% of its pixels do: past a driver's rounding, and ten times
 * under the smallest feature a picture frames. A failure writes the frame and where it differs to
 * test_pictures/ in the build tree.
 *
 * @param seen   The frame.
 * @param name   The reference's name.
 * @param device The GPU it was drawn on.
 */
void matchesReference(const Picture& seen, const std::string& name, const Device& device);

} // namespace Vkm::Test
