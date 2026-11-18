#include "offline/gl_air_baker.h"

#include "vkm_opengl/core/context.h"

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

GLAirBaker::GLAirBaker()
    : m_transmittance("shaders/opengl/offline/atmosphere/transmittance")
    , m_multiScattering("shaders/opengl/offline/atmosphere/multiscatter") {}

GLAirBaker::~GLAirBaker() = default;

void GLAirBaker::bake(
    Vkm::OpenGL::Context& gl,
    GLAtmosphere& atmosphere,
    const Atmosphere::Coefficients& air
) {
    atmosphere.createAir();
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    atmosphere.bindFbo();
    m_transmittance.bind();
    GLAtmosphere::setExtinction(m_transmittance, air);
    atmosphere.attachTransmittance(gl);
    m_lutTri.draw();

    m_multiScattering.bind();
    GLAtmosphere::setAir(m_multiScattering, air);
    atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);
    atmosphere.attachMultiScattering(gl);
    m_lutTri.draw();
    atmosphere.unbindFbo();
}

} // namespace Vkm::Engine
