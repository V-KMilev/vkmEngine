#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe.h"

#include "logger.h"

namespace Engine {

void GLProbe::createTargets() {
    if (m_captureFbo) return;  // already allocated

    m_captureFbo = std::make_unique<Core::FrameBuffer>();

    m_envCube.create(ENV_SIZE,         ENV_MIPS,       GL_RGB16F, GL_RGB, GL_FLOAT, true);
    m_irradiance.create(IRRADIANCE_SIZE, 1,            GL_RGB16F, GL_RGB, GL_FLOAT, false);
    m_prefilter.create(PREFILTER_SIZE,  PREFILTER_MIPS, GL_RGB16F, GL_RGB, GL_FLOAT, true);

    // Shared depth for the six geometry captures (so closer surfaces win).
    // Convolution passes run depth-off and ignore it.
    Core::Texture2DParams depth;
    depth.width          = ENV_SIZE;
    depth.height         = ENV_SIZE;
    depth.internalFormat = GL_DEPTH_COMPONENT24;
    depth.format         = GL_DEPTH_COMPONENT;
    depth.type           = GL_FLOAT;
    depth.minFilter      = Core::TextureMinFilter::Nearest;
    depth.magFilter      = Core::TextureMagFilter::Nearest;
    depth.wrapS          = Core::TextureWrap::ClampToEdge;
    depth.wrapT          = Core::TextureWrap::ClampToEdge;
    depth.generateMipmaps = false;
    m_depth = std::make_unique<Core::Texture2D>("probe_depth", depth);

    m_captureFbo->bind();
    m_captureFbo->attachTexture2D(GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth->getID(), 0);
    m_captureFbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_captureFbo->setReadBuffer(GL_COLOR_ATTACHMENT0);
    m_captureFbo->unbind();
}

} // namespace Engine
