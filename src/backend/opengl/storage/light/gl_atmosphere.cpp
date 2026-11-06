#include "storage/light/gl_atmosphere.h"

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader_base.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/mip_chain_texture.h"
#include "vkm_opengl/texture/texture.h"
#include "vkm_opengl/texture/texture_3d.h"

namespace Vkm::Engine {

GLAtmosphere::GLAtmosphere()  = default;
GLAtmosphere::~GLAtmosphere() = default;

void GLAtmosphere::setExtinction(const Vkm::OpenGL::ShaderBase& shader, const Atmosphere::Coefficients& air) {
    shader.setUniform3fv("u_rayleighScattering", air.rayleighScattering);
    shader.setUniform3fv("u_mieExtinction",      air.mieExtinction);
}

void GLAtmosphere::setAir(const Vkm::OpenGL::ShaderBase& shader, const Atmosphere::Coefficients& air) {
    setExtinction(shader, air);
    shader.setUniform3fv("u_mieScattering", air.mieScattering);
}

void GLAtmosphere::setSky(
    const Vkm::OpenGL::ShaderBase& shader,
    const SkyParams& sky,
    const glm::vec3& illuminance
) {
    setAir(shader, sky.air);
    shader.setUniform3fv("u_sunIlluminance", illuminance);
}

void GLAtmosphere::createAir() {
    if (m_transmittance) return;
    m_fbo = std::make_unique<Vkm::OpenGL::FrameBuffer>();
    // RGBA16F, as GL makes RGB16F color-renderable only optionally.
    m_transmittance = std::make_unique<Vkm::OpenGL::Texture2D>(
        "sky_transmittance_lut",
        Vkm::OpenGL::renderTargetParams(
            TRANSMITTANCE_WIDTH,
            TRANSMITTANCE_HEIGHT,
            GL_RGBA16F,
            GL_RGBA,
            Vkm::OpenGL::TextureMinFilter::Linear,
            Vkm::OpenGL::TextureMagFilter::Linear
        )
    );
    m_multiScattering = std::make_unique<Vkm::OpenGL::Texture2D>(
        "sky_multiscattering_lut",
        Vkm::OpenGL::renderTargetParams(
            MULTISCATTERING_SIZE,
            MULTISCATTERING_SIZE,
            GL_RGBA16F,
            GL_RGBA,
            Vkm::OpenGL::TextureMinFilter::Linear,
            Vkm::OpenGL::TextureMagFilter::Linear
        )
    );
}

void GLAtmosphere::createView() {
    if (m_skyView) return;
    m_skyView = std::make_unique<Vkm::OpenGL::MipChainTexture>();
    m_skyView->create(SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 1, GL_RGBA16F, GL_LINEAR, GL_LINEAR);

    Vkm::OpenGL::Texture3DParams volume;
    volume.width        = AERIAL_PERSPECTIVE_SIZE;
    volume.height       = AERIAL_PERSPECTIVE_SIZE;
    volume.depth        = AERIAL_PERSPECTIVE_SIZE;
    m_aerialPerspective = std::make_unique<Vkm::OpenGL::Texture3D>("sky_aerial_perspective", volume);
}

void GLAtmosphere::bindFbo() const {
    if (m_fbo) m_fbo->bind();
}

void GLAtmosphere::unbindFbo() const {
    if (m_fbo) m_fbo->unbind();
}

void GLAtmosphere::attachTransmittance(const Vkm::OpenGL::Context& gl) const {
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT0, m_transmittance->getID());
    gl.setViewport(0, 0, TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void GLAtmosphere::attachMultiScattering(const Vkm::OpenGL::Context& gl) const {
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT0, m_multiScattering->getID());
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
