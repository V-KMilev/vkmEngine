#pragma once

#include <array>
#include <cstdint>

#include "vkm_opengl/buffer/readback_buffer.h"
#include "vkm_opengl/buffer/shader_storage_buffer.h"
#include "vkm_opengl/shader/compute_shader.h"

#include "system/render/exposure.h"

namespace Vkm::Engine {

class GLTarget;
struct RenderSettings;

/**
 * @brief Auto-exposure's measurement (RenderSettings::autoExposure); the adapting is
 *        Exposure::Adaptation's.
 *
 * Up to MEASURE_RATE times a second the scene's HDR color is counted into a histogram of log2 luminance on
 * the GPU (shaders/opengl/post/exposure/histogram) and copied into one of READBACKS buffers, which the CPU
 * reads once its fence has passed: up to READBACKS frames late, and the frame never waits. Owns GL objects,
 * so construct it where a context is current.
 */
class GLExposure {
    public:
        GLExposure();
        ~GLExposure();

        GLExposure(const GLExposure& other) = delete;
        GLExposure& operator=(const GLExposure& other) = delete;

        GLExposure(GLExposure && other) = delete;
        GLExposure& operator=(GLExposure && other) = delete;

    public:
        /**
         * @brief The stops this frame is exposed by (Exposure::Adaptation::update), from the newest
         *        histogram read back.
         *
         * @param settings The frame's.
         * @param seconds  Wall-clock time since the last frame.
         * @return The stops, RenderSettings::exposure included.
         */
        float stops(const RenderSettings& settings, float seconds);

        /**
         * @brief Count @p scene's color into the histogram and start reading it back.
         *
         * Skipped while every readback buffer is still waiting on the GPU, or MEASURE_RATE has
         * been reached.
         *
         * @param scene   The HDR scene, before bloom and the tonemap.
         * @param width   Its width in pixels.
         * @param height  Its height in pixels.
         * @param seconds Wall-clock time since the last frame.
         */
        void measure(const GLTarget& scene, uint32_t width, uint32_t height, float seconds);

        /// Take the next read whole, as for a replaced world (Exposure::Adaptation::forget).
        void forget();

    private:
        static constexpr int READBACKS = 3;  ///< Readback buffers in flight: the frames the GPU may run
                                             ///< behind the CPU by.

        static constexpr float MEASURE_RATE = 60.0f;  ///< Times a second at most a frame is counted: at
                                                      ///< 60 fps every frame, faster no more often, as the
                                                      ///< eye adapts over seconds.

        /// Drop every copy in flight, so none is read after a pause in measuring.
        void dropPending();

    private:
        Vkm::OpenGL::ComputeShader       m_histogram;
        Vkm::OpenGL::ShaderStorageBuffer m_bins;  ///< The frame's histogram, the dispatch's target.

        std::array<Vkm::OpenGL::ReadbackBuffer, READBACKS> m_readbacks;
        int                                                m_next = 0;     ///< The next to fill: the oldest
                                                                           ///< in flight while all are.
        float                                              m_owed = 1.0f;  ///< Counts the time since the last
                                                                           ///< has earned, at MEASURE_RATE;
                                                                           ///< one is due at a whole.

        Exposure::Adaptation m_adaptation;
};

} // namespace Vkm::Engine
