#pragma once

#include "vkm_opengl/shader/shader.h"

#include "gl_screen_triangle.h"
#include "storage/light/gl_atmosphere.h"
#include "system/sky/atmosphere.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

/**
 * @brief Baker for the atmosphere's two tables that depend on the air alone, into GLAtmosphere.
 *
 * Run when the air changes; the sky's per-frame tables and the IBL's procedural bake read what it
 * leaves. Lives as long as the backend, as its programs do.
 */
class GLAirBaker {
    public:
        GLAirBaker();
        ~GLAirBaker();

        GLAirBaker(const GLAirBaker& other) = delete;
        GLAirBaker& operator=(const GLAirBaker& other) = delete;

        GLAirBaker(GLAirBaker && other) = delete;
        GLAirBaker& operator=(GLAirBaker && other) = delete;

    public:
        /**
         * @brief Bake the atmosphere's transmittance table, then its multiple-scattering table from it.
         *
         * @param gl         Live GL context the tables draw through.
         * @param atmosphere Holds the tables.
         * @param air        The air they integrate.
         */
        void bake(Vkm::OpenGL::Context& gl, GLAtmosphere& atmosphere, const Atmosphere::Coefficients& air);

    private:
        Vkm::OpenGL::Shader m_transmittance;
        Vkm::OpenGL::Shader m_multiScattering;
        GLScreenTriangle    m_lutTri;  ///< For the tables.
};

} // namespace Vkm::Engine
