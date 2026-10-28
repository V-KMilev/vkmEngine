#pragma once

#include <vector>

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
 * Convolves an environment cubemap into an irradiance cube and a roughness-mipped prefilter
 * cube. Only the destination differs between callers, so each supplies a Target and the loop
 * lives here once. This owns the two convolution programs and the shared projection + face-view
 * basis (computed once), and draws the six faces with the backend's unit cube. One is shared (see
 * GLBackend), so the programs are compiled once.
 *
 * The caller owns GL state: bind the capture FBO and set depth/cull/blending off first. slices()
 * cuts a convolution into steps, and run() takes one into a Target: the mirror level, an
 * irradiance face, or a band of a prefilter level's face, which it scissors, turning the scissor
 * off again. A bake spread over frames runs a step a frame; convolve() runs them all.
 */
class GLCubeConvolver {
    public:
        /// The most texels of a prefilter level's face one step draws; a larger face takes bands.
        static constexpr int BAND_TEXELS = 128 * 256;

        /// One frame's share of a convolution spread over frames.
        struct Step {
            enum class Kind {
                Mirror,      ///< The prefilter's mirror level, every face.
                Irradiance,  ///< An irradiance face.
                Prefilter    ///< A band of rows of a prefilter level's face, past the mirror level.
            };

            Kind kind  = Kind::Mirror;
            int  face  = 0;
            int  mip   = 0;
            int  size  = 0;  ///< The level's face size in texels.
            int  mips  = 1;  ///< The levels the whole prefilter fills.
            int  part  = 0;  ///< A prefilter step's band.
            int  parts = 1;  ///< Bands in the face.
        };

        /// What a convolution reads and draws into; run() decides when.
        class Target {
            public:
                Target() = default;
                virtual ~Target() = default;

                Target(const Target& other) = delete;
                Target& operator=(const Target& other) = delete;

                Target(Target && other) = delete;
                Target& operator=(Target && other) = delete;

            public:
                /// Make the source cube's mips.
                virtual void makeSourceMips() = 0;
                /// Bind the source cube at BakeTextureSlots::SOURCE.
                virtual void bindSource() = 0;
                /// Attach irradiance face @p face as color 0.
                virtual void attachIrradiance(int face) = 0;
                /// Attach prefilter face @p face of level @p mip as color 0.
                virtual void attachPrefilter(int face, int mip) = 0;
        };

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
         * @brief How many bands a prefilter face of @p size texels is drawn in.
         *
         * @param size The level's face size in texels.
         * @return One, or as many as keep a band within BAND_TEXELS.
         */
        static constexpr int bands(int size) {
            return size * size > BAND_TEXELS ? size * size / BAND_TEXELS : 1;
        }

        /**
         * @brief How many steps slices() cuts a convolution into.
         *
         * @param prefilterSize The prefilter's mirror-level face size in texels.
         * @param mips          Its levels.
         * @return The mirror level, six irradiance faces and every prefilter band past the mirror.
         */
        static constexpr int sliceCount(int prefilterSize, int mips) {
            int prefilter = 0;
            for (int mip = 1; mip < mips; ++mip) prefilter += bands(prefilterSize >> mip);
            return 1 + 6 + 6 * prefilter;
        }

        /**
         * @brief Cut the convolution of a prefilter of @p prefilterSize and @p mips into steps.
         *
         * @param prefilterSize The prefilter's mirror-level face size in texels.
         * @param mips          Its levels.
         * @return sliceCount() steps, in the order they must run: the mirror level first.
         */
        static std::vector<Step> slices(int prefilterSize, int mips);

        /**
         * @brief Take one step of a convolution into @p target.
         *
         * The mirror step makes the source's mips first, which every step after it reads.
         *
         * @param gl     Live GL context a band is scissored through.
         * @param step   One of slices()'s steps.
         * @param target What the step reads and draws into.
         */
        void run(Vkm::GL::Context& gl, const Step& step, Target& target);

        /**
         * @brief Run every step of a convolution into @p target, at once.
         *
         * @param gl            Live GL context.
         * @param prefilterSize The prefilter's mirror-level face size in texels.
         * @param mips          Its levels, from mirror-smooth to fully rough.
         * @param target        What the convolution reads and draws into.
         */
        void convolve(Vkm::GL::Context& gl, int prefilterSize, int mips, Target& target);

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
         * @brief Convolve one irradiance face.
         *
         * The caller has bound the env cube at BakeTextureSlots::SOURCE.
         *
         * @param face   Cube face index (0..5).
         * @param target Attaches the destination face.
         */
        void irradianceFace(int face, Target& target);

        /**
         * @brief Prefilter one band of rows of one face and level.
         *
         * The caller has bound the env cube at BakeTextureSlots::SOURCE.
         *
         * @param gl     Live GL context the band is scissored through.
         * @param face   Cube face index (0..5).
         * @param mip    The level, whose roughness is roughnessOf()'s for it.
         * @param mips   The levels the whole prefilter fills.
         * @param size   The level's face size in texels.
         * @param band   Which band of rows, from 0.
         * @param bands  How many bands the level's face is drawn in.
         * @param target Attaches the destination face and level.
         */
        void prefilterBand(
            Vkm::GL::Context& gl,
            int face,
            int mip,
            int mips,
            int size,
            int band,
            int bands,
            Target& target
        );

        /**
         * @brief The roughness the prefilter gives level @p mip of @p mips.
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
