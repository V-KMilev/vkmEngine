#pragma once

#include <string>
#include <vector>

#include "gl_shader.h"
#include "gl_screen_triangle.h"
#include "storage/gl_atmosphere.h"
#include "storage/gl_ibl.h"
#include "system/sky/atmosphere.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLCubeConvolver;

/**
 * @brief Baker for the IBL product set (split-sum), and for the atmosphere's two air tables.
 *
 * Borrows the backend's convolver for the convolution loops and the unit cube. Lives as long as
 * the backend: the procedural sky re-bakes whenever the sun moves, and a transient baker would
 * recompile its programs each time. Every bake fills GLIBL's back set and swaps it in once
 * complete. bake() and bakeProcedural() do it at once: the source into the env cube, then
 * irradiance and GGX-prefiltered specular. beginProcedural() and advance() do it a step at a time
 * (Unreal's time-sliced sky capture) - the env cube, a share of an irradiance face, a band of a
 * prefilter level - so a sun moving across the sky costs no frame the whole bake.
 * integrateBrdf() fills the BRDF LUT once at start.
 */
class GLIBLBaker {
    public:
        /// Each irradiance face is convolved in this many shares of its azimuths.
        static constexpr int IRRADIANCE_SLICES = 4;
        /// The most texels of a prefilter level's face one step draws; a larger face takes bands.
        static constexpr int BAND_TEXELS = 128 * 256;

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
         * @brief How many bands a prefilter level's face is drawn in.
         *
         * @param mip The level.
         * @return One, or as many as keep a band within BAND_TEXELS.
         */
        static constexpr int prefilterBands(int mip) {
            const int size = GLIBL::PREFILTER_SIZE >> mip;
            return size * size > BAND_TEXELS ? size * size / BAND_TEXELS : 1;
        }

        /**
         * @brief The steps, and so the frames, of a bake spread over frames.
         *
         * @return The env cube, its mips with the mirror level, every irradiance slice and every
         *         prefilter band past the mirror level.
         */
        static constexpr int slicedSteps() {
            int prefilter = 0;
            for (int mip = 1; mip < GLIBL::PREFILTER_MIPS; ++mip) prefilter += prefilterBands(mip);
            return 1 + 1 + 6 * IRRADIANCE_SLICES + 6 * prefilter;
        }

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
        bool baking() const { return m_next < m_steps.size(); }

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
        /// One frame's share of a bake spread over frames.
        struct Step {
            enum class Kind {
                Capture,     ///< The env cube's six faces from the sky.
                Mips,        ///< The env mips, and the prefilter's mirror level from them.
                Irradiance,  ///< A share of an irradiance face's azimuths.
                Prefilter    ///< A band of rows of a prefilter level's face.
            };

            Kind kind  = Kind::Capture;
            int  face  = 0;
            int  mip   = 0;
            int  part  = 0;  ///< The slice or band.
            int  parts = 1;  ///< Slices or bands in the face.
        };

    private:
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

        /**
         * @brief Take one step of a bake spread over frames.
         *
         * @param gl   Live GL context the step draws through.
         * @param ibl  The product set baked into.
         * @param step The step.
         */
        void runStep(Vkm::GL::Context& gl, GLIBL& ibl, const Step& step);

    private:
        Vkm::GL::Shader m_equirect;
        Vkm::GL::Shader m_sky;
        Vkm::GL::Shader m_transmittance;
        Vkm::GL::Shader m_multiScattering;
        Vkm::GL::Shader m_brdf;

        GLCubeConvolver& m_convolver;  ///< Irradiance + prefilter convolution, and the unit cube.
        ScreenTriangle   m_lutTri;     ///< For the LUTs.

        std::vector<Step> m_steps;     ///< A bake spread over frames, in order.
        size_t            m_next = 0;  ///< The next of m_steps; past the end when none is under way.
        SkyParams         m_target;    ///< What the bake spread over frames makes.
};

} // namespace Vkm::Engine
