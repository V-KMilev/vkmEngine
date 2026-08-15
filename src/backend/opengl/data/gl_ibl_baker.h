#pragma once

#include <string>

#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_screen_triangle.h"

#include "data/gl_cube_convolver.h"

namespace Core {
    class Context;
}

namespace Engine {

class GLIBL;

/**
 * @brief Parameters for the procedural-atmosphere bake (see bakeProcedural).
 */
struct SkyParams {
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the sun, normalized.
    float     sunIntensity = 22.0f;      ///< Top-of-atmosphere sun radiance scale.
    float     rayleigh     = 1.0f;       ///< Rayleigh (blue-sky) scattering scale.
    float     mie          = 1.0f;       ///< Mie (haze / sun glow) scattering scale.
    float     mieG         = 0.76f;      ///< Mie phase asymmetry.

    // Night. The atmosphere alone is nearly black with the sun down, so what
    // lights a night scene is authored: a skyglow floor plus a broad moon lobe.
    glm::vec3 nightRadiance{0.0f};
    glm::vec3 moonDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the moon, normalized.
    float     moonHalo = 0.0f;  ///< Radiance of the glow around the moon, not the disc itself.
};

/**
 * @brief Baker for the IBL product set (split-sum).
 *
 * Owns the four bake programs, a unit cube (the six face captures) and a
 * fullscreen triangle (the BRDF LUT). Construct it where a GL context is
 * current (GLBackend member). It stays alive for the backend's lifetime:
 * the procedural sky re-bakes whenever the sun moves, so a transient baker
 * would recompile all five programs on every rebake.
 *
 * bake(): load the equirect HDR -> render the environment cubemap -> convolve
 * diffuse irradiance -> GGX-prefilter specular mips -> integrate the BRDF/DFG
 * LUT. Results land in the passed GLIBL, which the forward + skybox passes
 * sample. A load failure leaves the GLIBL not-ready (forward falls back to flat
 * ambient).
 */
class GLIBLBaker {
    public:
        GLIBLBaker();
        ~GLIBLBaker();

        GLIBLBaker(const GLIBLBaker& other) = delete;
        GLIBLBaker& operator=(const GLIBLBaker& other) = delete;

        GLIBLBaker(GLIBLBaker && other) = delete;
        GLIBLBaker& operator=(GLIBLBaker && other) = delete;

        /**
         * @brief Bake @p ibl from the HDR at @p path. No-op (GLIBL stays not-ready)
         * if the file fails to load.
         */
        void bake(Core::Context& gl, GLIBL& ibl, const std::string& path);

        /**
         * @brief Bake @p ibl from a procedural Rayleigh + Mie atmosphere.
         *
         * Renders the analytic sky into the environment cubemap (in place of the
         * equirect step), then runs the same irradiance / prefilter / BRDF bake -
         * so ambient lighting and the skybox follow the atmosphere.
         */
        void bakeProcedural(Core::Context& gl, GLIBL& ibl, const SkyParams& sky);

    private:
        /**
         * @brief Draw the six env-cube faces with the already-bound @p shader
         * (u_projection + per-face u_view), then build its mip chain. Shared by
         * the equirect and procedural-sky captures.
         */
        void captureEnvFaces(Core::Context& gl, GLIBL& ibl, Core::Shader& shader);

        /**
         * @brief Convolve the env cube into diffuse irradiance + GGX prefilter and
         * integrate the split-sum BRDF/DFG LUT. The tail shared by both bakes.
         */
        void convolve(Core::Context& gl, GLIBL& ibl);

        Core::Shader m_equirect;
        Core::Shader m_sky;
        Core::Shader m_brdf;

        GLCubeConvolver      m_convolver;  ///< Shared irradiance + prefilter convolution (and the unit cube)
        Core::ScreenTriangle m_brdfTri;    ///< Attribute-less fullscreen triangle for the BRDF LUT
};

} // namespace Engine
