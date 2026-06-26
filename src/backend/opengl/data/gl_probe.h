#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "texture/gl_texture.h"  // Core::Texture2D (capture depth)
#include "gl_frame_buffer.h"
#include "gl_context.h"            // Core::Context (per-attach viewport sizing)
#include "gl_texture_cube.h"        // Core::TextureCube (transient capture cube)
#include "gl_texture_cube_array.h"  // Core::TextureCubeArray (irradiance + prefilter)

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
 * Both arrays are Core::TextureCubeArray and the env cube / depth / FBO are RAII,
 * so this class holds no raw GL handles - only the per-attach viewport sizing.
 */
class GLProbeArray {
    public:
        GLProbeArray() = default;
        ~GLProbeArray() = default;

        GLProbeArray(const GLProbeArray& other) = delete;
        GLProbeArray& operator=(const GLProbeArray& other) = delete;

        GLProbeArray(GLProbeArray && other) = delete;
        GLProbeArray& operator=(GLProbeArray && other) = delete;

        static constexpr int ENV_SIZE        = 256;  ///< Capture env cube face size
        static constexpr int ENV_MIPS        = 5;    ///< Env cube mips (prefilter source)
        static constexpr int IRRADIANCE_SIZE = 32;   ///< Irradiance face size
        static constexpr int PREFILTER_SIZE  = 128;  ///< Prefilter base face size
        static constexpr int PREFILTER_MIPS  = 5;    ///< Roughness mips; MAX_PROBE_LOD = this - 1

        /**
         * @brief Allocate the arrays (@p capacity layers each) + env cube + depth + FBO.
         * Idempotent.
         */
        void createTargets(int capacity);
        int  capacity() const { return m_capacity; }

        // Bake render-target ops: attach a face/layer, sized to its viewport.
        void bindCaptureFbo()   const { m_fbo->bind(); }
        void unbindCaptureFbo() const { m_fbo->unbind(); }

        /**
         * @brief Attach the transient env cube @p face as colour 0 (geometry capture).
         *
         * @param gl   Live GL context whose viewport is set to the env-capture size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachEnvFace(const Core::Context& gl, int face) const {
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_env.id(), 0);
            gl.setViewport(0, 0, ENV_SIZE, ENV_SIZE);
        }
        void generateEnvMips() const { m_env.generateMipmaps(); }

        /**
         * @brief Attach one face of irradiance-array @p layer as colour 0.
         *
         * @param gl    Live GL context whose viewport is set to the irradiance size.
         * @param layer Cube-array layer (the probe slot) being baked.
         * @param face  Cube face index (0..5) attached as the colour-0 target.
         */
        void attachIrradianceFace(const Core::Context& gl, int layer, int face) const {
            m_irradiance.attachFace(GL_COLOR_ATTACHMENT0, layer, face, 0);
            gl.setViewport(0, 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE);
        }
        /**
         * @brief Attach one face/mip of prefilter-array @p layer as colour 0.
         *
         * @param gl    Live GL context whose viewport is set to the mip's size.
         * @param layer Cube-array layer (the probe slot) being baked.
         * @param face  Cube face index (0..5) attached as the colour-0 target.
         * @param mip   Roughness mip level being baked; halves the viewport per level.
         */
        void attachPrefilterFace(const Core::Context& gl, int layer, int face, int mip) const {
            m_prefilter.attachFace(GL_COLOR_ATTACHMENT0, layer, face, mip);
            const int s = PREFILTER_SIZE >> mip;
            gl.setViewport(0, 0, s, s);
        }

        // Sampler binds for the forward pass.
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindEnvCube(uint32_t slot)    const { m_env.bindSlot(slot); }

    private:
        Core::TextureCubeArray m_irradiance;  ///< Diffuse irradiance, 1 mip per cube
        Core::TextureCubeArray m_prefilter;   ///< GGX specular, PREFILTER_MIPS per cube
        Core::TextureCube      m_env;         ///< Transient capture cube (reused per bake)

        std::unique_ptr<Core::Texture2D>   m_depth;
        std::unique_ptr<Core::FrameBuffer> m_fbo;
        int m_capacity = 0;
};

} // namespace Engine
