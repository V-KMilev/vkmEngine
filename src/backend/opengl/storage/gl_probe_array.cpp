#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/gl_probe_array.h"

#include <algorithm>

#include "logger.h"

#include "gl_error_handle.h"

#include "convention/gl_depth.h"
#include "storage/gl_ibl.h"

namespace Vkm::Engine {

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
    if (m_capacity == capacity && m_resolution == resolution) return;
    m_capacity   = capacity;
    m_resolution = resolution;

    // RGBA16F is the guaranteed colour-renderable HDR format (RGB16F render
    // support is optional), for the capture cube too. create() releases any
    // prior allocation, so this doubles as a rebuild.
    m_irradiance.create(irradianceSize(), 1, m_capacity + 1, GL_RGBA16F);
    m_prefilter.create(prefilterSize(), PREFILTER_MIPS, m_capacity + 1, GL_RGBA16F);
    m_env.create(m_resolution, fullMipChain(m_resolution), GL_RGBA16F, true);

    // Shared capture depth for the six geometry captures (convolution runs
    // depth-off and ignores it).
    Vkm::GL::Texture2DParams depth;
    depth.width           = m_resolution;
    depth.height          = m_resolution;
    depth.internalFormat  = GLDepth::EYE_FORMAT;
    depth.format          = GL_DEPTH_COMPONENT;
    depth.type            = GL_FLOAT;
    depth.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    depth.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    depth.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
    depth.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    depth.generateMipmaps = false;
    m_depth = std::make_unique<Vkm::GL::Texture2D>("probe_depth", depth);

    m_fbo = std::make_unique<Vkm::GL::FrameBuffer>();
    m_fbo->attachTexture(GL_DEPTH_ATTACHMENT, m_depth->getID());
    m_fbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_fbo->setReadBuffer(GL_COLOR_ATTACHMENT0);

    LOG_INFO(
        "Probe array allocated: %d layers and one staging @ res %d (prefilter %d/%d mips, irradiance %d)",
        capacity,
        m_resolution,
        prefilterSize(),
        PREFILTER_MIPS,
        irradianceSize()
    );
}

void GLProbeArray::copyLayer(int from, int to) const {
    // A layer is its cube's six faces, from layer * 6.
    const auto copy = [&](const Vkm::GL::TextureCubeArray& array, int mip) {
        const int size = array.size() >> mip;
        VKM_GL_CHECK(
            glCopyImageSubData(
                array.getID(),
                GL_TEXTURE_CUBE_MAP_ARRAY,
                mip,
                0,
                0,
                from * 6,
                array.getID(),
                GL_TEXTURE_CUBE_MAP_ARRAY,
                mip,
                0,
                0,
                to * 6,
                size,
                size,
                6
            )
        );
    };
    copy(m_irradiance, 0);
    for (int mip = 0; mip < PREFILTER_MIPS; ++mip) copy(m_prefilter, mip);
}

} // namespace Vkm::Engine
