#pragma once

#include <functional>
#include <memory>

#include <glm/glm.hpp>

#include "gl_shader.h"

namespace Engine {

class GLMesh;

/**
 * @brief Shared IBL cube convolution: diffuse irradiance + GGX prefilter.
 *
 * Both the global IBL baker and the per-probe baker convolve an environment
 * cubemap into an irradiance cube and a roughness-mipped prefilter cube with
 * identical loops - only the destination differs (a cube FBO face for the IBL
 * baker, a cube-array layer+face for the probe baker). This owns the two
 * convolution programs and the unit cube the six face captures draw, plus the
 * shared projection + face-view basis (computed once). Each baker supplies the
 * env-bind + face-attach as callbacks, so the loop lives here once.
 *
 * The caller owns GL state: bind the capture FBO and set depth/cull off first;
 * these methods only set uniforms, run the caller's attach, and draw.
 */
class GLCubeConvolver {
    public:
        GLCubeConvolver();
        ~GLCubeConvolver();

        GLCubeConvolver(const GLCubeConvolver& other) = delete;
        GLCubeConvolver& operator=(const GLCubeConvolver& other) = delete;

        GLCubeConvolver(GLCubeConvolver && other) = delete;
        GLCubeConvolver& operator=(GLCubeConvolver && other) = delete;

        using BindEnv       = std::function<void()>;          ///< Bind the source env cube to unit 0.
        using AttachFace    = std::function<void(int face)>;
        using AttachMipFace = std::function<void(int face, int mip)>;

        /** @brief Convolve the env cube into six diffuse-irradiance faces. */
        void irradiance(const BindEnv& bindEnv, const AttachFace& attach);

        /** @brief GGX prefilter into six faces per mip, roughness 0..1 across @p mips. */
        void prefilter(int mips, const BindEnv& bindEnv, const AttachMipFace& attach);

        /**
         * @brief The unit cube the captures draw. Reused by the IBL baker's equirect +
         * env-capture steps and the probe baker's skybox draw, so there is one
         * cube per baker rather than one per step.
         */
        const GLMesh& cube() const { return *m_cube; }

        /**
         * @brief The shared 90deg convolution projection + per-face view basis, also
         * reused by the IBL baker's equirect / env-capture face loops.
         */
        const glm::mat4& projection() const { return m_projection; }
        const glm::mat4& faceView(int face) const { return m_faceViews[face]; }

    private:
        Core::Shader m_irradiance;
        Core::Shader m_prefilter;

        std::unique_ptr<GLMesh> m_cube;

        glm::mat4 m_projection;     ///< convolveProjection(), once.
        glm::mat4 m_faceViews[6];   ///< origin-eye face views, once.
};

} // namespace Engine
