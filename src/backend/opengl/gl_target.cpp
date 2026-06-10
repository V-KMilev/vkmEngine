#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_target.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"  // Core::Texture2D

namespace Engine {

GLTarget::GLTarget() = default;
GLTarget::~GLTarget() = default;

void GLTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    // HDR color texture: linear filter, clamp, no mips.
    Core::Texture2DParams params;
    params.width          = width;
    params.height         = height;
    params.internalFormat = GL_RGBA16F;
    params.format         = GL_RGBA;
    params.type           = GL_FLOAT;
    params.minFilter      = Core::TextureMinFilter::Linear;
    params.magFilter      = Core::TextureMagFilter::Linear;
    params.wrapS          = Core::TextureWrap::ClampToEdge;
    params.wrapT          = Core::TextureWrap::ClampToEdge;
    params.generateMipmaps = false;
    m_color = std::make_unique<Core::Texture2D>("scene_hdr", params);

    m_depth.storage(GL_DEPTH_COMPONENT24, static_cast<int32_t>(width), static_cast<int32_t>(height));

    m_fbo.bind();
    m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_color->getID());
    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depth.getID());
    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u)", width, height);
    }
    m_fbo.unbind();
}

void GLTarget::bind(const Core::Context& gl) const {
    m_fbo.bind();
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::bindColor(uint32_t slot) const {
    if (m_color) m_color->bindSlot(slot);
}

void GLTarget::blitColorFrom(const GLTarget& src) const {
    // Read/draw-FBO binds have no Core helper, but they belong here (GLTarget
    // owns the FBOs) rather than leaking into pass code; the blit itself uses
    // the Core::FrameBuffer wrapper.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.m_fbo.getID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo.getID());
    Core::FrameBuffer::blit(
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

} // namespace Engine
