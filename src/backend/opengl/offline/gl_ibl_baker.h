#pragma once

#include <string>
#include <vector>

#include "gl_shader.h"
#include "gl_screen_triangle.h"
#include "offline/gl_cube_convolver.h"
#include "storage/gl_atmosphere.h"
#include "storage/gl_ibl.h"
#include "system/sky/atmosphere.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

/**
 * @brief Baker for the IBL product set (split-sum), and for the atmosphere's two air tables.
 *
 * Borrows the backend's convolver for the convolution loops and the unit cube. Lives as long as
 * the backend: the procedural sky re-bakes whenever the sun moves, and a transient baker would
 * recompile its programs each time. Every bake fills GLIBL's back set and swaps it in once
 * complete. bake() and bakeProcedural() do it at once: the source into the env cube, then
 * irradiance and GGX-prefiltered specular. beginProcedural() and advance() do it a step at a time
 * (Unreal's time-sliced sky capture) - the env cube, then GLCubeConvolver::slices's steps - so a
 * sun moving across the sky costs no frame the whole bake.
 * integrateBrdf() fills the BRDF LUT once at start.
 */
class GLIBLBaker {
    public:
        /// A sky shown this many degrees behind the one asked for takes a step more a frame.
        static constexpr float CATCH_UP_DEGREES = 1.5f;
        /// The most steps a frame takes (why this many: engine.md, the sky's lighting row).
        static constexpr int   MAX_CATCH_UP_STEPS = 6;

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
         * @brief The steps, and so the frames, of a bake spread over frames.
         *
         * @return The env cube, then the convolution's steps (GLCubeConvolver::slices).
         */
        static constexpr int slicedSteps() {
            return 1 + GLCubeConvolver::sliceCount(GLIBL::PREFILTER_SIZE, GLIBL::PREFILTER_MIPS);
        }

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
         * @brief Bake @p ibl from the HDR at @p path, at once.
         *
         * A file that fails to load leaves the GLIBL not ready, whatever it held. Ends any bake
         * spread over frames.
         *
         * @param gl   Live GL context the bake draws through.
         * @param ibl  The product set baked into.
         * @param path The equirectangular HDR to bake from.
         */
        void bake(Vkm::GL::Context& gl, GLIBL& ibl, const std::string& path);

        /**
         * @brief Bake the atmosphere's transmittance table, then its multiple-scattering table from it.
         *
         * @param gl         Live GL context the tables draw through.
         * @param atmosphere Holds the tables.
         * @param air        The air they integrate.
         */
        void bakeAir(Vkm::GL::Context& gl, GLAtmosphere& atmosphere, const Atmosphere::Coefficients& air);

        /**
         * @brief Bake @p ibl from the procedural Rayleigh, Mie and ozone atmosphere, at once.
         *
         * Paints the env cube from the atmosphere's air tables, which bakeAir has made for
         * @p sky's air, in the equirect step's place; the convolution is the same. Ends any bake
         * spread over frames.
         *
         * @param gl         Live GL context the bake draws through.
         * @param ibl        The product set baked into.
         * @param atmosphere Holds the air tables the sky is painted from.
         * @param sky        The atmosphere, and the sun and moon it is lit by.
         */
        void bakeProcedural(
            Vkm::GL::Context& gl,
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
        bool advance(Vkm::GL::Context& gl, GLIBL& ibl, const GLAtmosphere& atmosphere);

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
        void integrateBrdf(Vkm::GL::Context& gl, GLIBL& ibl);

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
         * @brief Draw env-cube @p face with the bound, parameterised @p source program.
         *
         * @param gl     Live GL context the face draws through.
         * @param ibl    The product set whose back env cube is filled.
         * @param source The bound program that paints the environment.
         * @param face   Cube face index (0..5).
         */
        void captureFace(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source, int face);

        /**
         * @brief Run the whole bake from a bound, parameterised environment @p source program.
         *
         * Allocates the back set, captures and convolves the cube, and swaps it in. Bakes
         * differ only in the program that paints the environment.
         *
         * @param gl     Live GL context the bake draws through.
         * @param ibl    The product set baked into.
         * @param source The bound program that paints the environment.
         */
        void bakeFrom(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source);

    private:
        Vkm::GL::Shader m_equirect;
        Vkm::GL::Shader m_sky;
        Vkm::GL::Shader m_transmittance;
        Vkm::GL::Shader m_multiScattering;
        Vkm::GL::Shader m_brdf;

        GLCubeConvolver& m_convolver;  ///< Irradiance + prefilter convolution, and the unit cube.
        ScreenTriangle   m_lutTri;     ///< For the LUTs.

        /// The convolution's share of a bake spread over frames, after the env cube's step.
        std::vector<GLCubeConvolver::Step> m_steps;

        int       m_next = 0;  ///< The next step, 0 the env cube's; idle() when none is under way.
        SkyParams m_target;    ///< What the bake spread over frames makes.
};

} // namespace Vkm::Engine
