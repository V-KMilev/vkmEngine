#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "gl_texture.h"
#include "gl_frame_buffer.h"
#include "gl_texture_cube.h"
#include "gl_context.h"

namespace Vkm::Engine {

/**
 * @brief GPU-side image-based lighting product set (split-sum).
 *
 * Owns every texture the split-sum path needs plus the shared capture FBO; the
 * environment cubemap also feeds the skybox.
 *
 * GLIBLBaker drives each bake through the ops below - createTargets (a no-op
 * once allocated), attachEnvFace, generateEnvMips, attachIrradianceFace,
 * attachPrefilterFace - and never touches raw GL. isReady() is false until a
 * successful bake, and false again once the scene asks for no environment or a
 * bake's source fails to load: what the cubes still hold is then a sky the
 * scene no longer has.
 *
 * The BRDF/DFG LUT is apart from all that: a function of view angle and
 * roughness alone, which no environment enters, integrated once when the
 * backend starts (createBrdf, attachBrdf) and bound by bindBrdf whether or
 * not a sky was ever baked.
 */
class GLIBL {
    public:
        static constexpr int ENV_SIZE        = 512;  ///< Environment cubemap face size
        static constexpr int ENV_MIPS        = 6;    ///< Env cube mip count (prefilter source)
        static constexpr int IRRADIANCE_SIZE = 32;   ///< Diffuse irradiance face size
        static constexpr int PREFILTER_SIZE  = 512;  ///< Prefiltered specular base face size
        /// Roughness mip count, 512 down to 8.
        static constexpr int PREFILTER_MIPS  = 7;
        static constexpr int BRDF_SIZE       = 512;  ///< BRDF/DFG LUT size

        GLIBL() = default;
        ~GLIBL() = default;

        GLIBL(const GLIBL& other) = delete;
        GLIBL& operator=(const GLIBL& other) = delete;

        GLIBL(GLIBL && other) = delete;
        GLIBL& operator=(GLIBL && other) = delete;

    public:
        /**
         * @brief Allocate the environment's cubes, and the capture FBO if createBrdf has not.
         *
         * Idempotent: a second call with the targets already allocated is a no-op.
         */
        void createTargets();

        /**
         * @brief Allocate the BRDF/DFG LUT, and the capture FBO it is integrated through.
         *
         * Idempotent, like createTargets.
         */
        void createBrdf();

        void markReady()   { m_ready = true; }
        void markUnready() { m_ready = false; }

        // The shared capture FBO every face and mip attach targets; the baker
        // holds no GL state itself.
        void bindCaptureFbo()   const { m_captureFbo->bind(); }
        void unbindCaptureFbo() const { m_captureFbo->unbind(); }

        /**
         * @brief Attach env cube @p face as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the env face size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachEnvFace(const Vkm::GL::Context& gl, int face) const {
            m_captureFbo->attachTexture2D(
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                m_envCube.getID(),
                0
            );
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
            m_captureFbo->attachTexture2D(
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                m_irradiance.getID(),
                0
            );
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
            m_captureFbo->attachTexture2D(
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                m_prefilter.getID(),
                mip
            );
            const int s = PREFILTER_SIZE >> mip;
            gl.setViewport(0, 0, s, s);
        }

        /**
         * @brief Attach the BRDF/DFG LUT as colour 0, sized to its viewport.
         *
         * @param gl Live GL context whose viewport is set to the LUT size.
         */
        void attachBrdf(const Vkm::GL::Context& gl) const {
            m_captureFbo->attachTexture2D(
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                m_brdf ? m_brdf->getID() : 0,
                0
            );
            gl.setViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
        }

        // Sampler binds.
        void bindEnvCube(uint32_t slot)    const { m_envCube.bindSlot(slot); }
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindBrdf(uint32_t slot)       const { if (m_brdf) m_brdf->bindSlot(slot); }

        bool isReady() const { return m_ready; }

    private:
        Vkm::GL::TextureCube                  m_envCube;
        Vkm::GL::TextureCube                  m_irradiance;
        Vkm::GL::TextureCube                  m_prefilter;
        std::unique_ptr<Vkm::GL::Texture2D>   m_brdf;
        std::unique_ptr<Vkm::GL::FrameBuffer> m_captureFbo;

        bool m_ready = false;
};

} // namespace Vkm::Engine
