#pragma once

#include <memory>
#include <string>

#include "gl_shader.h"
#include "gl_screen_triangle.h"

namespace Core {
    class Context;
}

namespace Engine {

class GLIBL;
class GLMesh;

/**
 * @brief One-shot baker for the IBL product set (split-sum).
 *
 * Owns the four bake programs, a unit cube (the six face captures) and a
 * fullscreen triangle (the BRDF LUT). Construct it where a GL context is
 * current (GLBackend::init), call bake() once, and let it fall out of scope -
 * the bake-only programs/meshes don't linger past startup.
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

        /// Bake @p ibl from the HDR at @p path. No-op (GLIBL stays not-ready)
        /// if the file fails to load.
        void bake(Core::Context& gl, GLIBL& ibl, const std::string& path);

    private:
        Core::Shader m_equirect;
        Core::Shader m_irradiance;
        Core::Shader m_prefilter;
        Core::Shader m_brdf;

        std::unique_ptr<GLMesh>     m_cube;     ///< Unit cube for the six face captures
        Core::ScreenTriangle        m_brdfTri;  ///< Attribute-less fullscreen triangle for the BRDF LUT
};

} // namespace Engine
