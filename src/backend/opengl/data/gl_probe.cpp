#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe.h"

#include "logger.h"

namespace Engine {

void GLProbeArray::createTargets(int capacity) {
    if (m_capacity > 0) return;  // already allocated
    m_capacity = capacity;

    // RGBA16F is the guaranteed colour-renderable HDR format (RGB16F render
    // support is optional). Each array holds m_capacity cubes (capacity * 6 faces).
    m_irradiance.create(IRRADIANCE_SIZE, 1, m_capacity, GL_RGBA16F);
    m_prefilter.create(PREFILTER_SIZE, PREFILTER_MIPS, m_capacity, GL_RGBA16F);
    m_env.create(ENV_SIZE, ENV_MIPS, GL_RGB16F, GL_RGB, GL_FLOAT, true);

    // Shared capture depth for the six geometry captures (convolution runs
    // depth-off and ignores it).
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

    m_fbo = std::make_unique<Core::FrameBuffer>();
    m_fbo->bind();
    m_fbo->attachTexture2D(GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth->getID(), 0);
    m_fbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_fbo->setReadBuffer(GL_COLOR_ATTACHMENT0);
    m_fbo->unbind();

    LOG_INFO("Probe array allocated: %d layers (irradiance %d, prefilter %d/%d mips)",
        capacity, IRRADIANCE_SIZE, PREFILTER_SIZE, PREFILTER_MIPS);
}

} // namespace Engine
