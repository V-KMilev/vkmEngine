#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "gl_shader.h"

namespace Vkm::GL {
    class Context;
}

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
 * The caller owns GL state: bind the capture FBO and set depth/cull/blending
 * off first; these methods set uniforms, run the caller's attach, and draw.
 * The two that convolve a share of a face (irradianceSlice, prefilterBand), for
 * a bake spread over frames, also blend or scissor, and turn it off again.
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
         * @brief Convolve one share of one irradiance face: the azimuths of @p slice of @p slices.
         *
         * Slice 0 writes the face and each later one adds to it, so all of them in order are
         * the face irradiance() draws. The caller has bound the env cube at
         * BakeTextureSlots::SOURCE.
         *
         * @param gl     Live GL context the later slices blend through.
         * @param face   Cube face index (0..5).
         * @param slice  Which share, from 0.
         * @param slices How many shares the face is convolved in.
         * @param attach Callback that attaches the destination face as colour 0.
         */
        void irradianceSlice(Vkm::GL::Context& gl, int face, int slice, int slices, const AttachFace& attach);

        /**
         * @brief Prefilter one band of rows of one face and level, as prefilter() draws them.
         *
         * The caller has bound the env cube at BakeTextureSlots::SOURCE.
         *
         * @param gl     Live GL context the band is scissored through.
         * @param face   Cube face index (0..5).
         * @param mip    The level, whose roughness is prefilter()'s for it.
         * @param mips   The levels the whole prefilter fills.
         * @param size   The level's face size in texels.
         * @param band   Which band of rows, from 0.
         * @param bands  How many bands the level's face is drawn in.
         * @param attach Callback that attaches the destination face + mip as colour 0.
         */
        void prefilterBand(
            Vkm::GL::Context& gl,
            int face,
            int mip,
            int mips,
            int size,
            int band,
            int bands,
            const AttachMipFace& attach
        );

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
        /**
         * @brief The roughness prefilter() gives level @p mip of @p mips.
         *
         * @param mip  The level.
         * @param mips The levels the prefilter fills.
         * @return 0 at the first level, rising evenly to 1 at the last.
         */
        static float roughnessOf(int mip, int mips);

    private:
        Vkm::GL::Shader m_irradiance;
        Vkm::GL::Shader m_prefilter;

        const GLMesh& m_cube;  ///< The backend's unit cube.

        glm::mat4 m_projection;     ///< convolveProjection(), once.
        glm::mat4 m_faceViews[6];   ///< Origin-eye face views, once.
};

} // namespace Vkm::Engine
