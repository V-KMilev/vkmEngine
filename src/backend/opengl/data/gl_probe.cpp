#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe.h"

#include "logger.h"

namespace Engine {

GLProbeArray::~GLProbeArray() {
    if (m_irradiance) glDeleteTextures(1, &m_irradiance);
    if (m_prefilter)  glDeleteTextures(1, &m_prefilter);
}

GLuint GLProbeArray::createCubeArray(int size, int mips) const {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, id);
    // Immutable storage; depth = capacity cubes * 6 faces. RGBA16F is the
    // guaranteed colour-renderable HDR format (RGB16F render support is optional).
    glTexStorage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mips, GL_RGBA16F, size, size, m_capacity * 6);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER,
        mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAX_LEVEL, mips - 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, 0);
    return id;
}

void GLProbeArray::createTargets(int capacity) {
    if (m_capacity > 0) return;  // already allocated
    m_capacity = capacity;

    m_irradiance = createCubeArray(IRRADIANCE_SIZE, 1);
    m_prefilter  = createCubeArray(PREFILTER_SIZE,  PREFILTER_MIPS);
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
