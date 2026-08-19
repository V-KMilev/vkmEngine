#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_target.h"

#include <algorithm>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"
#include "target/gl_render_buffer.h"

namespace Engine {

GLTarget::GLTarget() = default;
GLTarget::~GLTarget() = default;

namespace {
// One off-screen target texture, from vkmGL's render-target preset. Filter
// defaults to nearest (depth / G-buffer); the HDR colour target passes linear.
std::unique_ptr<Vkm::GL::Texture2D> makeTarget2D(
    const char* name,
    uint32_t w,
    uint32_t h,
    GLenum internalFormat,
    GLenum format,
    Vkm::GL::TextureMinFilter minFilter = Vkm::GL::TextureMinFilter::Nearest,
    Vkm::GL::TextureMagFilter magFilter = Vkm::GL::TextureMagFilter::Nearest) {
    return std::make_unique<Vkm::GL::Texture2D>(
        name, Vkm::GL::renderTargetParams(w, h, internalFormat, format, minFilter, magFilter));
}
} // namespace

void GLTarget::setSamples(uint32_t samples, const Vkm::GL::Context& gl) {
    // Clamp to the driver cap (cached inside the Context); 1 keeps the
    // single-sample path.
    samples = std::clamp(samples, 1u, static_cast<uint32_t>(gl.maxSamples()));
    if (samples == m_samples) return;
    m_samples = samples;
    m_width   = 0;  // force a rebuild on the next resize (dimensions unchanged)
}

void GLTarget::release() {
    // Deleting an attachment detaches it; the FBO object survives for the next
    // resize to re-attach to.
    m_color.reset();
    m_depth.reset();
    m_gbuffer.reset();
    m_colorRB.reset();
    m_depthRB.reset();
    m_gbufferRB.reset();
    m_width  = 0;
    m_height = 0;
}

void GLTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    m_fbo.bind();

    if (m_samples > 1) {
        // Renderbuffer attachments (not sampleable), resolved into a
        // single-sample GLTarget before any pass samples the scene. Same
        // formats as that path, so the resolve blit is a straight format match.
        const int32_t s = static_cast<int32_t>(m_samples);
        const int32_t w = static_cast<int32_t>(width);
        const int32_t h = static_cast<int32_t>(height);

        m_colorRB = std::make_unique<Vkm::GL::RenderBuffer>();
        m_colorRB->storageMultisample(s, GL_RGBA16F, w, h);
        m_fbo.attachRenderBuffer(GL_COLOR_ATTACHMENT0, m_colorRB->getID());

        m_depthRB = std::make_unique<Vkm::GL::RenderBuffer>();
        m_depthRB->storageMultisample(s, GL_DEPTH_COMPONENT24, w, h);
        m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depthRB->getID());

        if (m_hasGBuffer) {
            m_gbufferRB = std::make_unique<Vkm::GL::RenderBuffer>();
            m_gbufferRB->storageMultisample(s, GL_RGBA16F, w, h);
            m_fbo.attachRenderBuffer(GL_COLOR_ATTACHMENT1, m_gbufferRB->getID());
        }
    } else {
        // HDR colour: linear filter for sampling (composite/bloom), clamp, no mips.
        m_color = makeTarget2D("scene_hdr", width, height, GL_RGBA16F, GL_RGBA,
                               Vkm::GL::TextureMinFilter::Linear, Vkm::GL::TextureMagFilter::Linear);

        m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_color->getID());

        if (!m_colorOnly) {
            // Sampleable depth (24-bit), nearest so the post passes read exact depths.
            m_depth = makeTarget2D("scene_depth", width, height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT);
            m_fbo.attachTexture2D(GL_DEPTH_ATTACHMENT, m_depth->getID());
        }

        if (m_hasGBuffer) {
            // View normal (octahedral) in rg, roughness in b, metalness in a.
            m_gbuffer = makeTarget2D("scene_gbuffer", width, height, GL_RGBA16F, GL_RGBA);
            m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT1, m_gbuffer->getID());
        }
    }

    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u, %u samples)", width, height, m_samples);
    }
    m_fbo.unbind();
}

void GLTarget::bind(const Vkm::GL::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::bindGBufferPass(const Vkm::GL::Context& gl) {
    m_fbo.bind();
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT1);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::clearForFrame(const Vkm::GL::Context& gl) {
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
    // Vkm::GL::FrameBuffer wrappers - no raw GL.
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    src.m_fbo.bind(GL_READ_FRAMEBUFFER);
    Vkm::GL::FrameBuffer::blit(
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void GLTarget::resolveColorTo(GLTarget& dst) {
    if (m_samples <= 1) return;  // single-sample: the render target already is dst

    // A colour blit out of a multisample framebuffer is the resolve - GL does it
    // in the blit - so the only thing this adds to blitColorFrom is the guard.
    dst.blitColorFrom(*this);
}

void GLTarget::resolveGeometryTo(GLTarget& dst, bool gbuffer) {
    if (m_samples <= 1) return;

    const int32_t w = static_cast<int32_t>(m_width);
    const int32_t h = static_cast<int32_t>(m_height);

    // Depth: a depth blit ignores the colour read/draw buffer selection, so just
    // bind dst as draw and this as read.
    dst.m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_fbo.bind(GL_READ_FRAMEBUFFER);
    Vkm::GL::FrameBuffer::blit(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    // G-buffer (colour attachment 1): the read-buffer / draw-buffer selection is
    // per-framebuffer state that persists across binds, so set each on its own
    // FBO first, then split the read (this) and draw (dst) binds for the blit.
    if (gbuffer && m_hasGBuffer && dst.m_gbuffer) {
        m_fbo.setReadBuffer(GL_COLOR_ATTACHMENT1);       // this: read from colour 1
        dst.m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT1);   // dst: draw into colour 1 (draw FBO = dst)
        m_fbo.bind(GL_READ_FRAMEBUFFER);                 // read FBO = this (read buffer persists)
        Vkm::GL::FrameBuffer::blit(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        m_fbo.setReadBuffer(GL_COLOR_ATTACHMENT0);       // restore the default read buffer
    }
}

} // namespace Engine
