#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "texture/gl_texture.h"  // Core::Texture2D (capture depth)
#include "gl_frame_buffer.h"
#include "gl_texture_cube.h"     // Core::TextureCube (transient capture cube)

namespace Engine {

/**
 * @brief Shared GPU storage for every reflection probe: two cube-map arrays.
 *
 * One GL_TEXTURE_CUBE_MAP_ARRAY holds each probe's diffuse irradiance (one cube
 * per layer); another holds each probe's roughness-prefiltered specular (mipped).
 * The forward pass binds just these two samplers regardless of probe count and
 * samples layer = probe index - so the probe count is bounded by the layer count
 * + the per-fragment loop, not by texture units. A single transient env cube
 * (+ depth) is reused per bake: GLProbeBaker renders the scene into it, then
 * convolves into the target layer of each array.
 *
 * vkmGL has no cube-array wrapper, so the two arrays are managed with raw GL.
 */
class GLProbeArray {
    public:
        GLProbeArray() = default;
        ~GLProbeArray();

        GLProbeArray(const GLProbeArray&) = delete;
        GLProbeArray& operator=(const GLProbeArray&) = delete;
        GLProbeArray(GLProbeArray&&) = delete;
        GLProbeArray& operator=(GLProbeArray&&) = delete;

        static constexpr int ENV_SIZE        = 256;  ///< Capture env cube face size
        static constexpr int ENV_MIPS        = 5;    ///< Env cube mips (prefilter source)
        static constexpr int IRRADIANCE_SIZE = 32;   ///< Irradiance face size
        static constexpr int PREFILTER_SIZE  = 128;  ///< Prefilter base face size
        static constexpr int PREFILTER_MIPS  = 5;    ///< Roughness mips; MAX_PROBE_LOD = this - 1

        /// Allocate the arrays (@p capacity layers each) + env cube + depth + FBO.
        /// Idempotent.
        void createTargets(int capacity);
        int  capacity() const { return m_capacity; }

        // --- bake render-target ops ---
        void bindCaptureFbo()   const { m_fbo->bind(); }
        void unbindCaptureFbo() const { m_fbo->unbind(); }

        /// Attach the transient env cube @p face as colour 0 (geometry capture).
        void attachEnvFace(int face) const {
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_env.id(), 0);
            glViewport(0, 0, ENV_SIZE, ENV_SIZE);
        }
        void generateEnvMips() const { m_env.generateMipmaps(); }

        /// Attach one face of irradiance-array @p layer as colour 0.
        void attachIrradianceFace(int layer, int face) const {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                m_irradiance, 0, layer * 6 + face);
            glViewport(0, 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE);
        }
        /// Attach one face/mip of prefilter-array @p layer as colour 0.
        void attachPrefilterFace(int layer, int face, int mip) const {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                m_prefilter, mip, layer * 6 + face);
            const int s = PREFILTER_SIZE >> mip;
            glViewport(0, 0, s, s);
        }

        // --- sampler binds for the forward pass ---
        void bindIrradiance(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_irradiance);
        }
        void bindPrefilter(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_prefilter);
        }
        void bindEnvCube(uint32_t slot) const { m_env.bindSlot(slot); }

    private:
        GLuint createCubeArray(int size, int mips) const;

        GLuint m_irradiance = 0;   ///< GL_TEXTURE_CUBE_MAP_ARRAY (1 mip)
        GLuint m_prefilter  = 0;   ///< GL_TEXTURE_CUBE_MAP_ARRAY (PREFILTER_MIPS)
        Core::TextureCube m_env;   ///< Transient capture cube (reused per bake)

        std::unique_ptr<Core::Texture2D>   m_depth;
        std::unique_ptr<Core::FrameBuffer> m_fbo;
        int m_capacity = 0;
};

} // namespace Engine
