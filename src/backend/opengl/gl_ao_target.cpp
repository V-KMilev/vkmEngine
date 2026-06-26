#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_ao_target.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"

namespace Engine {

GLAOTarget::GLAOTarget()  = default;
GLAOTarget::~GLAOTarget() = default;

void GLAOTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    // Single AO factor in [0,1]. R16F keeps banding off the soft falloff; linear
    // filter so the forward sample (and any later half-res upsample) is smooth.
    Core::Texture2DParams p;
    p.width          = width;
    p.height         = height;
    p.internalFormat = GL_R16F;
    p.format         = GL_RED;
    p.type           = GL_FLOAT;
    p.minFilter      = Core::TextureMinFilter::Linear;
    p.magFilter      = Core::TextureMagFilter::Linear;
    p.wrapS          = Core::TextureWrap::ClampToEdge;
    p.wrapT          = Core::TextureWrap::ClampToEdge;
    p.generateMipmaps = false;
    m_tex = std::make_unique<Core::Texture2D>("scene_ao", p);

    m_fbo.bind();
    m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_tex->getID());
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLAOTarget framebuffer incomplete (%ux%u)", width, height);
    }
    m_fbo.unbind();
}

void GLAOTarget::bind(const Core::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLAOTarget::bindTexture(uint32_t slot) const {
    if (m_tex) m_tex->bindSlot(slot);
}

} // namespace Engine
