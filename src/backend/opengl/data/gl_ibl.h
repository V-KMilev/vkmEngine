#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "texture/gl_texture.h"
#include "gl_frame_buffer.h"
#include "gl_texture_cube.h"
#include "gl_context.h"

namespace Vkm::Engine {

/**
 * @brief GPU-side image-based lighting product set (split-sum).
 *
 * Owns every texture the split-sum path needs plus the shared capture FBO; the
 * environment cubemap also feeds the skybox. Everything is RAII, so there is no
 * manual GL cleanup.
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

        /**
         * @brief Record a successful bake so isReady() reports true.
         */
        void markReady() { m_ready = true; }

        bool isReady() const { return m_ready; }

        /**
         * @brief Allocate every GL texture + the capture FBO.
         *
         * Idempotent: a second call with the targets already allocated is a no-op.
         */
        void createTargets();

        /**
         * @brief Upload (or replace) the source equirectangular HDR as RGB16F.
         *
         * @param width  Source image width in pixels.
         * @param height Source image height in pixels.
         * @param rgb    Tightly packed float RGB pixels, width*height*3 long.
         */
        void uploadEquirect(uint32_t width, uint32_t height, const float* rgb);

        /**
         * @brief Bind / unbind the shared capture FBO that every face/mip attach
         * targets. The baker holds no GL state itself.
         */
        void bindCaptureFbo()   const { m_captureFbo->bind(); }
        void unbindCaptureFbo() const { m_captureFbo->unbind(); }

        /**
         * @brief Bind the source equirectangular HDR as a sampler input.
         *
         * @param slot Texture unit the equirect-to-cube shader samples from.
         */
        void bindEquirect(uint32_t slot) const {
            if (m_equirect) m_equirect->bindSlot(slot);
        }

        /**
         * @brief Attach env cube @p face as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the env face size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachEnvFace(const Vkm::GL::Context& gl, int face) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_envCube.id(), 0);
            gl.setViewport(0, 0, ENV_SIZE, ENV_SIZE);
        }
        /**
         * @brief Generate the env cube mip chain (prefilter source).
         */
        void generateEnvMips() const { m_envCube.generateMipmaps(); }

        /**
         * @brief Attach irradiance cube @p face as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the irradiance face size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachIrradianceFace(const Vkm::GL::Context& gl, int face) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_irradiance.id(), 0);
            gl.setViewport(0, 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE);
        }

        /**
         * @brief Attach prefilter cube @p face / @p mip as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the mip's size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         * @param mip  Roughness mip level being baked; halves the viewport per level.
         */
        void attachPrefilterFace(const Vkm::GL::Context& gl, int face, int mip) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_prefilter.id(), mip);
            const int s = PREFILTER_SIZE >> mip;
            gl.setViewport(0, 0, s, s);
        }

        /**
         * @brief Attach the BRDF/DFG LUT as colour 0, sized to its viewport.
         *
         * @param gl Live GL context whose viewport is set to the LUT size.
         */
        void attachBrdf(const Vkm::GL::Context& gl) const {
            m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_brdf ? m_brdf->getID() : 0, 0);
            gl.setViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
        }

        /**
         * @brief Sampler binds for the forward + skybox passes.
         *
         * @param slot Texture unit the forward/skybox shader samples the env cube from.
         */
        void bindEnvCube(uint32_t slot)    const { m_envCube.bindSlot(slot); }
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindBrdf(uint32_t slot)       const { if (m_brdf) m_brdf->bindSlot(slot); }

    private:
        std::unique_ptr<Vkm::GL::Texture2D>   m_equirect;
        Vkm::GL::TextureCube                  m_envCube;
        Vkm::GL::TextureCube                  m_irradiance;
        Vkm::GL::TextureCube                  m_prefilter;
        std::unique_ptr<Vkm::GL::Texture2D>   m_brdf;
        std::unique_ptr<Vkm::GL::FrameBuffer> m_captureFbo;

        bool m_ready = false;
};

} // namespace Vkm::Engine
