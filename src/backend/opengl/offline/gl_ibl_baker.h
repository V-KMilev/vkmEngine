#pragma once

#include <string>

#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_screen_triangle.h"
#include "system/sky/atmosphere.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLCubeConvolver;
class GLIBL;

/**
 * @brief Everything the procedural-atmosphere bake (see bakeProcedural) depends on.
 */
struct SkyParams {
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the sun, normalized.
    float     sunIntensity = 22.0f;      ///< Top-of-atmosphere sun radiance scale.
    /// The air the sky scatters through, already under the sky's scales.
    Atmosphere::Coefficients air{};
    float     mieG         = 0.76f;      ///< Mie phase asymmetry.

    // Night: the atmosphere is nearly black with the sun down, so a night scene's light is
    // authored, a skyglow floor plus a broad moon lobe.
    glm::vec3 nightRadiance{0.0f};
    glm::vec3 moonDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the moon, normalized.
    float     moonHalo = 0.0f;  ///< Radiance of the glow around the moon, not the disc itself.

    /// Cosine of the widest angle (about 0.57 degrees) two directions can be apart and bake the same.
    static constexpr float SAME_DIRECTION = 0.99995f;

    /**
     * @brief Whether two skies bake the same.
     *
     * Every term exact but the sun and moon directions, compared by angle against SAME_DIRECTION:
     * they move continuously, and an exact compare would bake every frame the sun animates.
     *
     * @param other The sky to compare against.
     * @return Whether a bake of @p other would produce this one.
     */
    bool operator==(const SkyParams& other) const {
        return sunIntensity  == other.sunIntensity
            && air.rayleighScattering == other.air.rayleighScattering
            && air.mieScattering      == other.air.mieScattering
            && air.mieExtinction      == other.air.mieExtinction
            && mieG          == other.mieG
            && nightRadiance == other.nightRadiance
            && moonHalo      == other.moonHalo
            && glm::dot(sunDir, other.sunDir) >= SAME_DIRECTION
            && glm::dot(moonDir, other.moonDir) >= SAME_DIRECTION;
    }
};

/**
 * @brief Baker for the IBL product set (split-sum).
 *
 * Borrows the backend's convolver for the convolution loops and the unit cube. Lives as long as
 * the backend: the procedural sky re-bakes whenever the sun moves, and a transient baker would
 * recompile its programs each time. bake() renders the HDR into the env cube, then convolves
 * irradiance and GGX-prefiltered specular; integrateBrdf() fills the LUT once at start.
 */
class GLIBLBaker {
    public:
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
         * @brief Bake @p ibl from the HDR at @p path.
         *
         * A file that fails to load leaves the GLIBL not ready, whatever it held.
         *
         * @param gl   Live GL context the bake draws through.
         * @param ibl  The product set baked into.
         * @param path The equirectangular HDR to bake from.
         */
        void bake(Vkm::GL::Context& gl, GLIBL& ibl, const std::string& path);

        /**
         * @brief Bake @p ibl from a procedural Rayleigh + Mie atmosphere.
         *
         * The analytic sky takes the equirect step's place; the convolution is the same.
         *
         * @param gl  Live GL context the bake draws through.
         * @param ibl The product set baked into.
         * @param sky The atmosphere, and the sun and moon it is lit by.
         */
        void bakeProcedural(Vkm::GL::Context& gl, GLIBL& ibl, const SkyParams& sky);

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
        /**
         * @brief Run the whole bake from a bound, parameterised environment @p source program.
         *
         * Allocates the targets, captures and convolves the cube, and marks @p ibl ready. Bakes
         * differ only in the program that paints the environment.
         *
         * @param gl     Live GL context the bake draws through.
         * @param ibl    The product set baked into.
         * @param source The bound program that paints the environment.
         */
        void bakeFrom(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source);

        /**
         * @brief Draw the six env-cube faces with the bound @p shader, then build its mip chain.
         *
         * @param gl     Live GL context the faces draw through.
         * @param ibl    The product set whose env cube is filled.
         * @param shader The bound program that paints each face.
         */
        void captureEnvFaces(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& shader);

        /**
         * @brief Convolve the env cube into diffuse irradiance + GGX prefilter.
         *
         * @param gl  Live GL context the convolutions draw through.
         * @param ibl The product set whose env cube is convolved.
         */
        void convolve(Vkm::GL::Context& gl, GLIBL& ibl);

    private:
        Vkm::GL::Shader m_equirect;
        Vkm::GL::Shader m_sky;
        Vkm::GL::Shader m_brdf;

        GLCubeConvolver& m_convolver;  ///< Irradiance + prefilter convolution, and the unit cube.
        ScreenTriangle   m_brdfTri;    ///< For the BRDF LUT.
};

} // namespace Vkm::Engine
