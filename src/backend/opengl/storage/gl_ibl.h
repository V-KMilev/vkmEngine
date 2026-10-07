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
 * environment cubemap also feeds the skybox under an HDR sky.
 *
 * The three cubes come in two sets. Readers bind the front set; a bake fills
 * the back set through the ops below - createTargets (a no-op once allocated),
 * attachEnvFace, generateEnvMips, bindBackEnvCube, attachIrradianceFace,
 * attachPrefilterFace - and swap() makes it the front. So a bake spread over
 * frames (GLIBLBaker::advance) never shows a half-made set, and GLIBLBaker
 * never touches raw GL. isReady() is false until a bake swaps, and false again
 * once the scene asks for no environment or a bake's source fails to load:
 * what the cubes still hold is then a sky the scene no longer has.
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
         * @brief Allocate both sets' cubes, and the capture FBO if createBrdf has not.
         *
         * Both at once, so the first bake spread over frames does not pay for its set in one of
         * them. Idempotent: a second call is a no-op.
         */
        void createTargets();

        /**
         * @brief Allocate the BRDF/DFG LUT, and the capture FBO it is integrated through.
         *
         * Idempotent, like createTargets.
         */
        void createBrdf();

        /**
         * @brief Make the back set the one readers bind, and the IBL ready.
         */
        void swap() {
            m_front = 1 - m_front;
            m_ready = true;
        }

        void markUnready() { m_ready = false; }

        // The shared capture FBO every face and mip attach targets; the baker
        // holds no GL state itself.
        void bindCaptureFbo()   const { m_captureFbo->bind(); }
        void unbindCaptureFbo() const { m_captureFbo->unbind(); }

        /**
         * @brief Attach the back env cube's @p face as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the env face size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachEnvFace(const Vkm::GL::Context& gl, int face) const {
            attachFace(gl, back().env, face, 0, ENV_SIZE);
        }

        /**
         * @brief Generate the back env cube's mip chain (prefilter source).
         */
        void generateEnvMips() const { back().env.generateMipmaps(); }

        /**
         * @brief Attach the back irradiance cube's @p face as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the irradiance face size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachIrradianceFace(const Vkm::GL::Context& gl, int face) const {
            attachFace(gl, back().irradiance, face, 0, IRRADIANCE_SIZE);
        }

        /**
         * @brief Attach the back prefilter cube's @p face / @p mip as colour 0, sized to its viewport.
         *
         * @param gl   Live GL context whose viewport is set to the mip's size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         * @param mip  Roughness mip level being baked; halves the viewport per level.
         */
        void attachPrefilterFace(const Vkm::GL::Context& gl, int face, int mip) const {
            attachFace(gl, back().prefilter, face, mip, PREFILTER_SIZE >> mip);
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

        /// The back env cube, as the source its convolutions read.
        void bindBackEnvCube(uint32_t slot) const { back().env.bindSlot(slot); }

        // Sampler binds, of the front set.
        void bindEnvCube(uint32_t slot)    const { front().env.bindSlot(slot); }
        void bindIrradiance(uint32_t slot) const { front().irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { front().prefilter.bindSlot(slot); }
        void bindBrdf(uint32_t slot)       const { if (m_brdf) m_brdf->bindSlot(slot); }

        bool isReady() const { return m_ready; }

    private:
        /// One complete product: what a bake fills and readers bind.
        struct Cubes {
            Vkm::GL::TextureCube env;
            Vkm::GL::TextureCube irradiance;
            Vkm::GL::TextureCube prefilter;
        };

    private:
        const Cubes& front() const { return m_cubes[m_front]; }
        const Cubes& back()  const { return m_cubes[1 - m_front]; }

        void attachFace(
            const Vkm::GL::Context& gl,
            const Vkm::GL::TextureCube& cube,
            int face,
            int mip,
            int size
        ) const;

    private:
        Cubes                                 m_cubes[2];
        std::unique_ptr<Vkm::GL::Texture2D>   m_brdf;
        std::unique_ptr<Vkm::GL::FrameBuffer> m_captureFbo;

        int  m_front = 0;
        bool m_ready = false;
};

} // namespace Vkm::Engine
