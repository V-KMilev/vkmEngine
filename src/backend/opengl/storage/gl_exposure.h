#pragma once

#include <array>
#include <cstdint>

#include <GL/glew.h>

#include "gl_compute_shader.h"
#include "gl_shader_storage_buffer.h"

#include "system/render/exposure.h"

namespace Vkm::Engine {

class GLTarget;
struct RenderSettings;

/**
 * @brief Auto-exposure's measurement (RenderSettings::autoExposure); the adapting is
 *        Exposure::Adaptation's.
 *
 * Each frame the scene's HDR colour is counted into a histogram of log2 luminance on the GPU
 * (shaders/exposure/histogram) and copied into one of READBACKS buffers, which the CPU reads once
 * its fence has passed: up to READBACKS frames late, and the frame never waits. Owns GL objects,
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
         * @brief Count @p scene's colour into the histogram and start reading it back.
         *
         * Skipped while every readback buffer is still waiting on the GPU.
         *
         * @param scene  The HDR scene, before bloom and the tonemap.
         * @param width  Its width in pixels.
         * @param height Its height in pixels.
         */
        void measure(const GLTarget& scene, uint32_t width, uint32_t height);

        /// Take the next read whole, as for a replaced world (Exposure::Adaptation::forget).
        void forget();

    private:
        /// Readback buffers in flight: the frames the GPU may run behind the CPU by.
        static constexpr int READBACKS = 3;

        /// One readback buffer, mapped for good (vkmGL has no persistent read mapping), and the
        /// fence the copy into it set.
        struct Readback {
            GLuint          buffer = 0;
            const uint32_t* bins   = nullptr;
            GLsync          fence  = nullptr;
        };

        /// Drop every copy in flight, so none is read after a pause in measuring.
        void dropPending();

    private:
        Vkm::GL::ComputeShader       m_histogram;
        Vkm::GL::ShaderStorageBuffer m_bins;  ///< The frame's histogram, the dispatch's target.

        std::array<Readback, READBACKS> m_readbacks{};
        int m_next = 0;  ///< The next to fill: the oldest in flight while all are.

        Exposure::Adaptation m_adaptation;
};

} // namespace Vkm::Engine
