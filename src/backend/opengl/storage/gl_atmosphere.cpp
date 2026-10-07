#include "storage/gl_atmosphere.h"

#include "gl_context.h"
#include "gl_frame_buffer.h"
#include "gl_mip_chain_texture.h"
#include "gl_shader_base.h"
#include "gl_texture.h"
#include "gl_texture_3d.h"

#include "storage/gl_lookup_table.h"

namespace Vkm::Engine {

GLAtmosphere::GLAtmosphere()  = default;
GLAtmosphere::~GLAtmosphere() = default;

void GLAtmosphere::setExtinction(const Vkm::GL::ShaderBase& shader, const Atmosphere::Coefficients& air) {
    shader.setUniform3fv("u_rayleighScattering", air.rayleighScattering);
    shader.setUniform3fv("u_mieExtinction",      air.mieExtinction);
}

void GLAtmosphere::setAir(const Vkm::GL::ShaderBase& shader, const Atmosphere::Coefficients& air) {
    setExtinction(shader, air);
    shader.setUniform3fv("u_mieScattering", air.mieScattering);
}

void GLAtmosphere::setSky(
    const Vkm::GL::ShaderBase& shader,
    const SkyParams& sky,
    const glm::vec3& illuminance
) {
    setAir(shader, sky.air);
    shader.setUniform1f("u_mieG",            sky.mieG);
    shader.setUniform3fv("u_sunIlluminance", illuminance);
}

void GLAtmosphere::createAir() {
    if (m_transmittance) return;
    m_fbo = std::make_unique<Vkm::GL::FrameBuffer>();
    // RGBA16F, as GL makes RGB16F colour-renderable only optionally.
    m_transmittance = std::make_unique<Vkm::GL::Texture2D>(
        "sky_transmittance_lut",
        lookupTableParams(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, GL_RGBA16F, GL_RGBA)
    );
    m_multiScattering = std::make_unique<Vkm::GL::Texture2D>(
        "sky_multiscattering_lut",
        lookupTableParams(MULTISCATTERING_SIZE, MULTISCATTERING_SIZE, GL_RGBA16F, GL_RGBA)
    );
}

void GLAtmosphere::createView() {
    if (m_skyView) return;
    m_skyView = std::make_unique<Vkm::GL::MipChainTexture>();
    m_skyView->create(SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 1, GL_RGBA16F, GL_LINEAR, GL_LINEAR);

    Vkm::GL::Texture3DParams volume;
    volume.width  = AERIAL_PERSPECTIVE_SIZE;
    volume.height = AERIAL_PERSPECTIVE_SIZE;
    volume.depth  = AERIAL_PERSPECTIVE_SIZE;
    m_aerialPerspective = std::make_unique<Vkm::GL::Texture3D>("sky_aerial_perspective", volume);
}

void GLAtmosphere::bindFbo() const {
    if (m_fbo) m_fbo->bind();
}

void GLAtmosphere::unbindFbo() const {
    if (m_fbo) m_fbo->unbind();
}

void GLAtmosphere::attachTransmittance(const Vkm::GL::Context& gl) const {
    m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_transmittance->getID(), 0);
    gl.setViewport(0, 0, TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void GLAtmosphere::attachMultiScattering(const Vkm::GL::Context& gl) const {
    m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_multiScattering->getID(), 0);
    gl.setViewport(0, 0, MULTISCATTERING_SIZE, MULTISCATTERING_SIZE);
}

void GLAtmosphere::bindTransmittance(uint32_t slot) const {
    if (m_transmittance) m_transmittance->bindSlot(slot);
}

void GLAtmosphere::bindMultiScattering(uint32_t slot) const {
    if (m_multiScattering) m_multiScattering->bindSlot(slot);
}

void GLAtmosphere::bindSkyView(uint32_t slot) const {
    if (m_skyView) m_skyView->bindSlot(slot);
}

void GLAtmosphere::bindAerialPerspective(uint32_t slot) const {
    if (m_aerialPerspective) m_aerialPerspective->bindSlot(slot);
}

void GLAtmosphere::bindSkyViewImage(uint32_t unit, GLenum access) const {
    if (m_skyView) m_skyView->bindImage(0, unit, access);
}

void GLAtmosphere::bindAerialPerspectiveImage(uint32_t unit, GLenum access) const {
    if (m_aerialPerspective) m_aerialPerspective->bindImage(unit, access);
}

} // namespace Vkm::Engine
