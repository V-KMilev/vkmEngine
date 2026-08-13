#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_mask_target.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"

namespace Engine {

GLMaskTarget::GLMaskTarget()  = default;
GLMaskTarget::~GLMaskTarget() = default;

void GLMaskTarget::setFormat(GLenum internalFormat, GLenum format) {
    if (internalFormat == m_internalFormat && format == m_format) return;
    m_internalFormat = internalFormat;
    m_format         = format;
    m_width          = 0;  // force a rebuild on the next resize
}

void GLMaskTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    // The occlusion factor in [0,1] (plus a packed bent normal when the format
    // says so). Float keeps banding off the soft falloff; linear filter so the
    // forward sample (and any later half-res upsample) is smooth.
    Core::Texture2DParams p;
    p.width          = width;
    p.height         = height;
    p.internalFormat = m_internalFormat;
    p.format         = m_format;
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
        LOG_ERROR("GLMaskTarget framebuffer incomplete (%ux%u)", width, height);
    }
    m_fbo.unbind();
}

void GLMaskTarget::bind(const Core::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLMaskTarget::bindTexture(uint32_t slot) const {
    if (m_tex) m_tex->bindSlot(slot);
}

} // namespace Engine
