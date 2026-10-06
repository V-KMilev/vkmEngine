#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "gl_shader.h"

namespace Vkm::Engine {

class GLMesh;

/**
 * @brief Shared IBL cube convolution: diffuse irradiance + GGX prefilter.
 *
 * Convolves an environment cubemap into an irradiance cube and a
 * roughness-mipped prefilter cube. Only the destination differs between
 * callers, so each supplies the face-attach as a callback and the loop lives
 * here once. This owns the two convolution programs and the shared projection
 * + face-view basis (computed once), and draws the six faces with the
 * backend's unit cube. One is shared (see GLBackend), so the programs are
 * compiled once.
 *
 * The caller owns GL state: bind the capture FBO and set depth/cull off first;
 * these methods only set uniforms, run the caller's attach, and draw.
 */
class GLCubeConvolver {
    public:
        using AttachFace    = std::function<void(int face)>;
        using AttachMipFace = std::function<void(int face, int mip)>;

        /**
         * @brief Compile the convolution programs, drawing each face with @p cube.
         *
         * @param cube The backend's unit cube, outliving this convolver.
         */
        explicit GLCubeConvolver(const GLMesh& cube);
        ~GLCubeConvolver();

        GLCubeConvolver(const GLCubeConvolver& other) = delete;
        GLCubeConvolver& operator=(const GLCubeConvolver& other) = delete;

        GLCubeConvolver(GLCubeConvolver && other) = delete;
        GLCubeConvolver& operator=(GLCubeConvolver && other) = delete;

    public:
        /**
         * @brief Convolve the env cube into six diffuse-irradiance faces.
         *
         * The caller has bound the env cube at BakeTextureSlots::SOURCE.
         *
         * @param attach Callback that attaches the destination face as colour 0.
         */
        void irradiance(const AttachFace& attach);

        /**
         * @brief GGX prefilter into six faces per mip, roughness 0..1 across @p mips.
         *
         * The caller has bound the env cube at BakeTextureSlots::SOURCE.
         *
         * @param mips   Mip levels to fill, from mirror-smooth to fully rough.
         * @param attach Callback that attaches the destination face + mip as colour 0.
         */
        void prefilter(int mips, const AttachMipFace& attach);

        /**
         * @brief The unit cube each face is drawn with.
         */
        const GLMesh& cube() const { return m_cube; }

        /**
         * @brief The shared 90deg convolution projection + per-face view basis.
         */
        const glm::mat4& projection() const { return m_projection; }
        const glm::mat4& faceView(int face) const { return m_faceViews[face]; }

    private:
        Vkm::GL::Shader m_irradiance;
        Vkm::GL::Shader m_prefilter;

        const GLMesh& m_cube;  ///< The backend's unit cube.

        glm::mat4 m_projection;     ///< convolveProjection(), once.
        glm::mat4 m_faceViews[6];   ///< Origin-eye face views, once.
};

} // namespace Vkm::Engine
