#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <GL/glew.h>

#include "texture/gl_texture.h"  // qualified: data/gl_texture.h (Engine GLTexture) shadows the bare name
#include "gl_frame_buffer.h"
#include "gl_texture_cube.h"

namespace Engine {

/**
 * @brief GPU-side image-based lighting product set (split-sum).
 *
 * Owns every texture the IBL path needs and the shared capture FBO: the
 * source equirectangular HDR (Core::Texture2D), the environment cubemap
 * (mipped, also feeds the skybox), the diffuse irradiance cubemap, the
 * prefiltered specular cubemap (roughness mips), and the BRDF/DFG lookup
 * (Core::Texture2D). The three cubemaps are Core::TextureCube; everything
 * is RAII, so there is no manual GL cleanup.
 *
 * GLIBLBaker calls createTargets() once, then drives the bake purely through
 * the bindX() / attachXFace() / generateEnvMips() ops below - it never touches
 * raw GL. The forward + skybox passes sample via bindEnvCube / bindIrradiance /
 * bindPrefilter / bindBrdf. isReady() is false until a successful bake.
 */
class GLIBL {
    public:
        GLIBL() = default;
        ~GLIBL() = default;

        GLIBL(const GLIBL& other) = delete;
        GLIBL& operator=(const GLIBL& other) = delete;

        GLIBL(GLIBL && other) = delete;
        GLIBL& operator=(GLIBL && other) = delete;

    public:
        static constexpr int ENV_SIZE        = 512;  ///< Environment cubemap face size
        static constexpr int ENV_MIPS        = 6;    ///< Env cube mip count (prefilter source)
        static constexpr int IRRADIANCE_SIZE = 32;   ///< Diffuse irradiance face size
        static constexpr int PREFILTER_SIZE  = 512;  ///< Prefiltered specular base face size
        static constexpr int PREFILTER_MIPS  = 7;    ///< Roughness mip count (512..8). MAX_REFLECTION_LOD in pbr shader = this - 1
        static constexpr int BRDF_SIZE       = 512;  ///< BRDF/DFG LUT size

        /// True when a (re)bake is required: a non-empty path that differs
        /// from what was last baked, or nothing has been baked yet.
        bool needsBake(const std::string& path) const;

        /// Record a successful bake of @p path so needsBake() goes quiet.
        void markBaked(const std::string& path);

        bool isReady() const { return m_ready; }

        /// Allocate every GL texture + the capture FBO. Idempotent.
        void createTargets();

        /// Upload (or replace) the source equirectangular HDR as RGB16F.
        void uploadEquirect(uint32_t width, uint32_t height, const float* rgb);

        /// Bake render-target ops (capture FBO + per-face/mip attach). Each
        /// attach also sizes the viewport to the target it points at, so the
        /// baker holds no GL state itself.
        void bindCaptureFbo()   const { m_captureFbo->bind(); }
        void unbindCaptureFbo() const { m_captureFbo->unbind(); }

        /// Bind the source equirectangular HDR as a sampler input.
        void bindEquirect(uint32_t slot) const {
            if (m_equirect) m_equirect->bindSlot(slot);
        }

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

        void attachBrdf() const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_brdf ? m_brdf->getID() : 0, 0);
            glViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
        }

        /// Sampler binds for the forward + skybox passes.
        void bindEnvCube(uint32_t slot)    const { m_envCube.bindSlot(slot); }
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindBrdf(uint32_t slot)       const { if (m_brdf) m_brdf->bindSlot(slot); }

    private:
        std::unique_ptr<Core::Texture2D>   m_equirect;
        Core::TextureCube                  m_envCube;
        Core::TextureCube                  m_irradiance;
        Core::TextureCube                  m_prefilter;
        std::unique_ptr<Core::Texture2D>   m_brdf;
        std::unique_ptr<Core::FrameBuffer> m_captureFbo;

        std::string m_bakedPath;
        bool        m_ready = false;
};

} // namespace Engine
