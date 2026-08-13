#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe.h"

#include <algorithm>

#include "logger.h"

namespace Engine {

int GLProbeArray::clampResolution(int resolution) {
    resolution = std::clamp(resolution, MIN_RESOLUTION, MAX_RESOLUTION);
    // Round down to a power of two (immutable cube-array storage needs a fixed
    // face size; power-of-two keeps the mip chain exact).
    int pot = MIN_RESOLUTION;
    while (pot * 2 <= resolution) pot *= 2;
    return pot;
}

void GLProbeArray::createTargets(int capacity, int resolution) {
    resolution = clampResolution(resolution);
    if (m_capacity == capacity && m_resolution == resolution) return;  // already current
    m_capacity   = capacity;
    m_resolution = resolution;

    // Capture face = resolution; prefilter/irradiance derive from it, preserving
    // the original 256/128/32 ratio (res/2, res/8).
    m_envSize        = resolution;
    m_prefilterSize  = std::max(1, resolution / 2);
    m_irradianceSize = std::max(1, resolution / 8);

    // RGBA16F is the guaranteed colour-renderable HDR format (RGB16F render
    // support is optional). Each array holds m_capacity cubes (capacity * 6 faces).
    // create() releases any prior allocation, so this doubles as a rebuild.
    m_irradiance.create(m_irradianceSize, 1, m_capacity, GL_RGBA16F);
    m_prefilter.create(m_prefilterSize, PREFILTER_MIPS, m_capacity, GL_RGBA16F);
    m_env.create(m_envSize, ENV_MIPS, GL_RGB16F, GL_RGB, GL_FLOAT, true);

    // Shared capture depth for the six geometry captures (convolution runs
    // depth-off and ignores it).
    Core::Texture2DParams depth;
    depth.width          = m_envSize;
    depth.height         = m_envSize;
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

    LOG_INFO("Probe array allocated: %d layers @ res %d (env %d, prefilter %d/%d mips, irradiance %d)",
        capacity, m_resolution, m_envSize, m_prefilterSize, PREFILTER_MIPS, m_irradianceSize);
}

} // namespace Engine
