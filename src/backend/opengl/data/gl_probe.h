#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "texture/gl_texture.h"
#include "gl_frame_buffer.h"
#include "gl_context.h"
#include "gl_texture_cube.h"
#include "gl_texture_cube_array.h"

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
 * Everything is RAII, so this class holds no raw GL handles - only the
 * per-attach viewport sizing.
 */
class GLProbeArray {
    public:
        GLProbeArray() = default;
        ~GLProbeArray() = default;

        GLProbeArray(const GLProbeArray& other) = delete;
        GLProbeArray& operator=(const GLProbeArray& other) = delete;

        GLProbeArray(GLProbeArray && other) = delete;
        GLProbeArray& operator=(GLProbeArray && other) = delete;

        static constexpr int ENV_MIPS       = 5;  ///< Env cube mips (prefilter source)
        static constexpr int PREFILTER_MIPS = 5;  ///< Roughness mips; MAX_PROBE_LOD = this - 1

        // Requested capture resolution bounds (the env cube face size). The
        // prefilter/irradiance faces derive from it (res/2, res/8), preserving
        // the original 256/128/32 ratio. Clamped to a power of two in range.
        static constexpr int MIN_RESOLUTION     = 128;
        static constexpr int MAX_RESOLUTION     = 1024;
        static constexpr int DEFAULT_RESOLUTION = 256;

        /**
         * @brief Clamp @p resolution to [MIN,MAX] and round down to a power of two.
         *
         * @param resolution Requested capture face size in pixels.
         * @return The face size actually usable for the shared cube arrays.
         */
        static int clampResolution(int resolution);

        /**
         * @brief Allocate the arrays (@p capacity layers each) + env cube + depth + FBO,
         * sized from @p resolution. Re-allocates when either changes; a no-op when
         * both match the current build.
         *
         * @param capacity   Probe-array layer count (probe slots).
         * @param resolution Capture face size; prefilter/irradiance derive from it.
         */
        void createTargets(int capacity, int resolution);
        int  capacity() const { return m_capacity; }
        int  resolution() const { return m_resolution; }
        int  envSize() const { return m_envSize; }

        // Bake render-target ops: attach a face/layer, sized to its viewport.
        void bindCaptureFbo()   const { m_fbo->bind(); }
        void unbindCaptureFbo() const { m_fbo->unbind(); }

        /**
         * @brief Attach the transient env cube @p face as colour 0 (geometry capture).
         *
         * @param gl   Live GL context whose viewport is set to the env-capture size.
         * @param face Cube face index (0..5) attached as the colour-0 target.
         */
        void attachEnvFace(const Vkm::GL::Context& gl, int face) const {
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_env.id(), 0);
            gl.setViewport(0, 0, m_envSize, m_envSize);
        }
        void generateEnvMips() const { m_env.generateMipmaps(); }

        /**
         * @brief Attach one face of irradiance-array @p layer as colour 0.
         *
         * @param gl    Live GL context whose viewport is set to the irradiance size.
         * @param layer Cube-array layer (the probe slot) being baked.
         * @param face  Cube face index (0..5) attached as the colour-0 target.
         */
        void attachIrradianceFace(const Vkm::GL::Context& gl, int layer, int face) const {
            m_irradiance.attachFace(GL_COLOR_ATTACHMENT0, layer, face, 0);
            gl.setViewport(0, 0, m_irradianceSize, m_irradianceSize);
        }
        /**
         * @brief Attach one face/mip of prefilter-array @p layer as colour 0.
         *
         * @param gl    Live GL context whose viewport is set to the mip's size.
         * @param layer Cube-array layer (the probe slot) being baked.
         * @param face  Cube face index (0..5) attached as the colour-0 target.
         * @param mip   Roughness mip level being baked; halves the viewport per level.
         */
        void attachPrefilterFace(const Vkm::GL::Context& gl, int layer, int face, int mip) const {
            m_prefilter.attachFace(GL_COLOR_ATTACHMENT0, layer, face, mip);
            const int s = m_prefilterSize >> mip;
            gl.setViewport(0, 0, s, s);
        }

        // Sampler binds for the forward pass.
        void bindIrradiance(uint32_t slot) const { m_irradiance.bindSlot(slot); }
        void bindPrefilter(uint32_t slot)  const { m_prefilter.bindSlot(slot); }
        void bindEnvCube(uint32_t slot)    const { m_env.bindSlot(slot); }

    private:
        Vkm::GL::TextureCubeArray m_irradiance;  ///< Diffuse irradiance, 1 mip per cube
        Vkm::GL::TextureCubeArray m_prefilter;   ///< GGX specular, PREFILTER_MIPS per cube
        Vkm::GL::TextureCube      m_env;         ///< Transient capture cube (reused per bake)

        std::unique_ptr<Vkm::GL::Texture2D>   m_depth;
        std::unique_ptr<Vkm::GL::FrameBuffer> m_fbo;
        int m_capacity   = 0;
        int m_resolution = 0;  ///< Current build's capture face size (0 = not built).
        int m_envSize        = 0;
        int m_irradianceSize = 0;
        int m_prefilterSize  = 0;
};

} // namespace Engine
