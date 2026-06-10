#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "texture/gl_texture.h"  // Core::Texture2D (depth); qualified - Engine data/gl_texture.h shadows the bare name
#include "gl_frame_buffer.h"
#include "gl_texture_cube.h"

namespace Engine {

/**
 * @brief One reflection probe's GPU product set.
 *
 * Mirrors the GLIBL cube set minus the equirect source + BRDF LUT (the global
 * IBL owns the shared BRDF). GLProbeBaker renders the scene into the env cube
 * (depth-tested), then convolves it into a diffuse irradiance cube and GGX-
 * prefilters it into a specular mip chain, reusing the ibl/irradiance +
 * ibl/prefilter shaders. The forward pass samples irradiance + prefilter for
 * surfaces inside the probe's influence box.
 *
 * The capture FBO keeps one shared depth texture (sized to the env face) so the
 * six geometry captures depth-test correctly; the convolution passes run with
 * depth off and ignore it.
 */
class GLProbe {
    public:
        GLProbe()  = default;
        ~GLProbe() = default;

        GLProbe(const GLProbe&) = delete;
        GLProbe& operator=(const GLProbe&) = delete;
        GLProbe(GLProbe&&) = delete;
        GLProbe& operator=(GLProbe&&) = delete;

    public:
        static constexpr int ENV_SIZE        = 256;  ///< Captured env cube face size
        static constexpr int ENV_MIPS        = 5;    ///< Env cube mips (prefilter source)
        static constexpr int IRRADIANCE_SIZE = 32;   ///< Diffuse irradiance face size
        static constexpr int PREFILTER_SIZE  = 128;  ///< Prefiltered specular base face size
        static constexpr int PREFILTER_MIPS  = 5;    ///< Roughness mips (128..8); MAX_PROBE_LOD = this - 1

        bool isReady() const { return m_ready; }
        void markBaked()     { m_ready = true; }

        /// Allocate the cubes, shared capture depth, and capture FBO. Idempotent.
        void createTargets();

        // --- bake render-target ops (capture FBO + per-face/mip attach) ---
        void bindCaptureFbo()   const { m_captureFbo->bind(); }
        void unbindCaptureFbo() const { m_captureFbo->unbind(); }

        /// Attach env-cube @p face as colour 0 (depth stays attached from setup).
        void attachEnvFace(int face) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_envCube.id(), 0);
            glViewport(0, 0, ENV_SIZE, ENV_SIZE);
        }
        void generateEnvMips() const { m_envCube.generateMipmaps(); }

        void attachIrradianceFace(int face) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_irradiance.id(), 0);
            glViewport(0, 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE);
        }

        void attachPrefilterFace(int face, int mip) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_prefilter.id(), mip);
            const int s = PREFILTER_SIZE >> mip;
            glViewport(0, 0, s, s);
        }

        // --- sampler binds for the forward pass ---
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindEnvCube(uint32_t slot)    const { m_envCube.bindSlot(slot); }

    private:
        Core::TextureCube m_envCube;
        Core::TextureCube m_irradiance;
        Core::TextureCube m_prefilter;

        std::unique_ptr<Core::Texture2D>   m_depth;       ///< Shared capture depth (ENV_SIZE)
        std::unique_ptr<Core::FrameBuffer> m_captureFbo;

        bool m_ready = false;
};

} // namespace Engine
