#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_target.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"  // Core::Texture2D

namespace Engine {

GLTarget::GLTarget() = default;
GLTarget::~GLTarget() = default;

namespace {
std::unique_ptr<Core::Texture2D> makeColor(const char* name, uint32_t w, uint32_t h,
                                           GLenum internalFormat, GLenum format) {
    Core::Texture2DParams p;
    p.width          = w;
    p.height         = h;
    p.internalFormat = internalFormat;
    p.format         = format;
    p.type           = GL_FLOAT;
    p.minFilter      = Core::TextureMinFilter::Nearest;
    p.magFilter      = Core::TextureMagFilter::Nearest;
    p.wrapS          = Core::TextureWrap::ClampToEdge;
    p.wrapT          = Core::TextureWrap::ClampToEdge;
    p.generateMipmaps = false;
    return std::make_unique<Core::Texture2D>(name, p);
}
}

void GLTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    // HDR colour: linear filter for sampling (composite/bloom), clamp, no mips.
    Core::Texture2DParams color;
    color.width = width; color.height = height;
    color.internalFormat = GL_RGBA16F; color.format = GL_RGBA; color.type = GL_FLOAT;
    color.minFilter = Core::TextureMinFilter::Linear;
    color.magFilter = Core::TextureMagFilter::Linear;
    color.wrapS = Core::TextureWrap::ClampToEdge;
    color.wrapT = Core::TextureWrap::ClampToEdge;
    color.generateMipmaps = false;
    m_color = std::make_unique<Core::Texture2D>("scene_hdr", color);

    // Sampleable depth (24-bit), nearest so SSR reads exact depths.
    m_depth = makeColor("scene_depth", width, height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT);

    m_fbo.bind();
    m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_color->getID());
    m_fbo.attachTexture2D(GL_DEPTH_ATTACHMENT,  m_depth->getID());

    if (m_hasGBuffer) {
        // View normal (octahedral) in rg, roughness in b, metalness in a.
        m_gbuffer = makeColor("scene_gbuffer", width, height, GL_RGBA16F, GL_RGBA);
        m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT1, m_gbuffer->getID());
    }

    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u)", width, height);
    }
    m_fbo.unbind();
}

void GLTarget::bind(const Core::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::bindGBufferPass(const Core::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT1);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::clearForFrame(const Core::Context& gl) {
    m_fbo.bind();
    if (m_hasGBuffer) {
        const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        m_fbo.setDrawBuffers(buffers, 2);
    } else {
        m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
    gl.clear(true, true, false);
}

void GLTarget::bindColor(uint32_t slot) const {
    if (m_color) m_color->bindSlot(slot);
}

void GLTarget::bindDepth(uint32_t slot) const {
    if (m_depth) m_depth->bindSlot(slot);
}

void GLTarget::bindGBuffer(uint32_t slot) const {
    if (m_gbuffer) m_gbuffer->bindSlot(slot);
}

void GLTarget::blitColorFrom(const GLTarget& src) {
    // setDrawBuffer binds this FBO and selects colour 0 (so we never blit into
    // the G-buffer); then point the READ framebuffer at src (its read buffer
    // defaults to colour 0) and blit. Order matters: setDrawBuffer binds
    // GL_FRAMEBUFFER (read + draw), so the read bind must come after it. All via
    // Core::FrameBuffer wrappers - no raw GL.
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    src.m_fbo.bind(GL_READ_FRAMEBUFFER);
    Core::FrameBuffer::blit(
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

} // namespace Engine
