#pragma once

#include <string>
#include <vector>

#include "vkm_opengl/shader/shader.h"

#include "gl_screen_triangle.h"
#include "offline/capture/gl_cube_convolver.h"
#include "storage/light/gl_atmosphere.h"
#include "storage/light/gl_ibl.h"
#include "system/sky/atmosphere.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

struct HDRImage;

/**
 * @brief Baker for the IBL product set (split-sum).
 *
 * Borrows the backend's convolver for the convolution loops and the unit cube. Lives as long as
 * the backend: the procedural sky re-bakes whenever the sun moves, and a transient baker would
 * recompile its programs each time. Every bake fills GLIBL's back set and swaps it in once
 * complete. bake() and bakeProcedural() do it at once: the source into the env cube, then
 * irradiance and GGX-prefiltered specular. beginProcedural() and advance() do it a step at a time
 * - the env cube, then GLCubeConvolver::slices's steps - so a sun moving across the sky costs no
 * frame the whole bake.
 * integrateBrdf() fills the BRDF LUT once at start.
 */
class GLIBLBaker {
    public:
        static constexpr float CATCH_UP_DEGREES   = 1.5f;  ///< A sky shown this many degrees behind the one
                                                           ///< asked for takes a step more a frame.
        static constexpr int   MAX_CATCH_UP_STEPS = 6;     ///< The most steps a frame takes, however far
                                                           ///< behind the shown sky is.

        /**
         * @brief Compile the bake programs, borrowing @p convolver for the convolution loops.
         *
         * @param convolver The backend's; outlives every baker that borrows it.
         */
        explicit GLIBLBaker(GLCubeConvolver& convolver);
        ~GLIBLBaker();

        GLIBLBaker(const GLIBLBaker& other) = delete;
        GLIBLBaker& operator=(const GLIBLBaker& other) = delete;

        GLIBLBaker(GLIBLBaker && other) = delete;
        GLIBLBaker& operator=(GLIBLBaker && other) = delete;

    public:
        /**
         * @brief The steps a frame takes on a bake toward @p asked while @p shown is shown.
         *
         * One while the shown sky's sun and moon are within CATCH_UP_DEGREES of the asked one's;
         * one more for each CATCH_UP_DEGREES further they have run ahead, up to
         * MAX_CATCH_UP_STEPS in all, so a time-lapse's light keeps pace.
         *
         * @param asked The sky the scene asks for.
         * @param shown The sky the shown cubes were baked from.
         * @return At least one.
         */
        static int stepsFor(const SkyParams& asked, const SkyParams& shown);

        /**
         * @brief Bake @p ibl from an equirectangular HDR image already decoded, at once.
         *
         * Ends any bake spread over frames.
         *
         * @param gl    Live GL context the bake draws through.
         * @param ibl   The product set baked into.
         * @param image The image; valid.
         * @param name  What the sky names it, for the log.
         */
        void bake(Vkm::OpenGL::Context& gl, GLIBL& ibl, const HDRImage& image, const std::string& name);

        /**
         * @brief Bake @p ibl from the procedural Rayleigh, Mie and ozone atmosphere, at once.
         *
         * Paints the env cube from the atmosphere's air tables, which GLAirBaker has made for
         * @p sky's air, in the equirect step's place; the convolution is the same. Ends any bake
         * spread over frames.
         *
         * @param gl         Live GL context the bake draws through.
         * @param ibl        The product set baked into.
         * @param atmosphere Holds the air tables the sky is painted from.
         * @param sky        The atmosphere, and the sun and moon it is lit by.
         */
        void bakeProcedural(
            Vkm::OpenGL::Context& gl,
            GLIBL& ibl,
            const GLAtmosphere& atmosphere,
            const SkyParams& sky
        );

        /**
         * @brief Start baking @p sky a step a frame; advance() takes each step.
         *
         * @param sky The atmosphere, and the sun and moon it is lit by.
         */
        void beginProcedural(const SkyParams& sky);

        /**
         * @brief Take the next step of the bake beginProcedural started, swapping it in after the last.
         *
         * @param gl         Live GL context the step draws through.
         * @param ibl        The product set baked into.
         * @param atmosphere Holds the air tables the sky is painted from, made for the bake's air.
         * @return Whether that was the last step, and @p ibl now shows the bake.
         */
        bool advance(Vkm::OpenGL::Context& gl, GLIBL& ibl, const GLAtmosphere& atmosphere);

        /**
         * @brief Whether a bake spread over frames has steps left.
         */
        bool baking() const { return m_next < idle(); }

        /**
         * @brief The sky the bake spread over frames is making, or last made.
         */
        const SkyParams& target() const { return m_target; }

        /**
         * @brief Integrate the split-sum BRDF/DFG LUT into @p ibl.
         *
         * Once, at backend start: the LUT depends on no environment and is read even with no sky.
         *
         * @param gl  Live GL context the integration draws through.
         * @param ibl The product set whose LUT is filled.
         */
        void integrateBrdf(Vkm::OpenGL::Context& gl, GLIBL& ibl);

    private:
        /// m_next past the last step: the env cube's, then m_steps.
        int idle() const { return 1 + static_cast<int>(m_steps.size()); }

        /**
         * @brief Bind the sky program with @p sky's uniforms and the air tables it reads.
         *
         * @param atmosphere Holds the air tables.
         * @param sky        The sky painted.
         */
        void bindSky(const GLAtmosphere& atmosphere, const SkyParams& sky);

        /**
         * @brief Draw env-cube @p face with the bound, parameterized @p source program.
         *
         * @param gl     Live GL context the face draws through.
         * @param ibl    The product set whose back env cube is filled.
         * @param source The bound program that paints the environment.
         * @param face   Cube face index (0..5).
         */
        void captureFace(Vkm::OpenGL::Context& gl, GLIBL& ibl, Vkm::OpenGL::Shader& source, int face);

        /**
         * @brief Run the whole bake from a bound, parameterized environment @p source program.
         *
         * Allocates the back set, captures and convolves the cube, and swaps it in. Bakes
         * differ only in the program that paints the environment.
         *
         * @param gl     Live GL context the bake draws through.
         * @param ibl    The product set baked into.
         * @param source The bound program that paints the environment.
         */
        void bakeFrom(Vkm::OpenGL::Context& gl, GLIBL& ibl, Vkm::OpenGL::Shader& source);

    private:
        Vkm::OpenGL::Shader m_equirect;
        Vkm::OpenGL::Shader m_sky;
        Vkm::OpenGL::Shader m_brdf;

        GLCubeConvolver& m_convolver;  ///< Irradiance + prefilter convolution, and the unit cube.
        GLScreenTriangle m_lutTri;     ///< For the LUTs.

        std::vector<GLCubeConvolver::Step> m_steps;  ///< The convolution's share of a bake spread over
                                                     ///< frames, after the env cube's step.

        int       m_next = 0;  ///< The next step, 0 the env cube's; idle() when none is under way.
        SkyParams m_target;    ///< What the bake spread over frames makes.
};

} // namespace Vkm::Engine
