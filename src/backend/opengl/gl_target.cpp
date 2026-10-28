#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_target.h"

#include <algorithm>
#include <iterator>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"

namespace Vkm::Engine {

namespace {

/// What each attachment is: where it attaches, what it stores, how it filters.
struct AttachmentSpec {
    GLenum      point;
    GLenum      format;
    bool        linear;  ///< Linear rather than nearest sampling, single-sample only.
    const char* name;
};

namespace Out = GLBindings::FragmentOutputs;

// Indexed by GLTarget::Attachment. HDR color filters linearly; depth and the data images are
// read texel for texel. The color format here is only the default GLTarget's constructor takes.
constexpr AttachmentSpec SPECS[] = {
    {GL_COLOR_ATTACHMENT0 + Out::COLOR,          GL_RGBA16F,           true,  "target_color"},
    {GL_DEPTH_ATTACHMENT,                        GLDepth::EYE_FORMAT,  false, "target_depth"},
    {GL_COLOR_ATTACHMENT0 + Out::GBUFFER,        GL_RG16F,             false, "target_gbuffer"},
    {GL_COLOR_ATTACHMENT0 + Out::REFLECT_WEIGHT, GL_RGBA8,             false, "target_reflect_weight"},
    {GL_COLOR_ATTACHMENT0 + Out::REFLECT_ENV,    GL_R11F_G11F_B10F,    false, "target_reflect_env"},
};
static_assert(
    std::size(SPECS) == static_cast<size_t>(GLTarget::Attachment::Count),
    "Every GLTarget attachment needs a spec"
);

const AttachmentSpec& specOf(GLTarget::Attachment attachment) {
    return SPECS[static_cast<size_t>(attachment)];
}

/// The attachment point of @p attachment, as a draw-buffer list names it.
GLenum pointOf(GLTarget::Attachment attachment) {
    return specOf(attachment).point;
}

} // namespace

GLTarget::GLTarget(Layout layout, GLenum colorFormat) : m_layout(layout), m_colorFormat(colorFormat) {}
GLTarget::~GLTarget() = default;

bool GLTarget::carries(Attachment attachment) const {
    switch (m_layout) {
        case Layout::Color:      return attachment == Attachment::Color;
        case Layout::ColorDepth: return attachment == Attachment::Color || attachment == Attachment::Depth;
        case Layout::Scene:
            return m_reflectInputs
                || (attachment != Attachment::ReflectWeight && attachment != Attachment::ReflectEnv);
    }
    return false;
}

const Vkm::GL::Texture2D* GLTarget::texture(Attachment attachment) const {
    return m_textures[static_cast<size_t>(attachment)].get();
}

void GLTarget::release() {
    // Deleting a texture detaches it only from the bound framebuffer; attached to another, its
    // storage lives on. So attachments are let go explicitly, keeping the FBO for the next
    // resize. A target holding nothing returns at once, so this is cheap every frame.
    const auto held = [](const std::unique_ptr<Vkm::GL::Texture2D>& texture) { return texture != nullptr; };
    if (std::none_of(m_textures.begin(), m_textures.end(), held)) return;

    for (size_t i = 0; i < ATTACHMENT_COUNT; ++i) detach(static_cast<Attachment>(i));
    m_width  = 0;
    m_height = 0;
}

void GLTarget::detach(Attachment attachment) {
    std::unique_ptr<Vkm::GL::Texture2D>& slot = m_textures[static_cast<size_t>(attachment)];
    if (!slot) return;
    m_fbo.attachTexture(pointOf(attachment), 0);
    slot.reset();
}

void GLTarget::resize(
    const Vkm::GL::Context& gl,
    uint32_t width,
    uint32_t height,
    uint32_t samples,
    bool reflectInputs
) {
    const bool inputsChange = reflectInputs != m_reflectInputs;
    m_reflectInputs = reflectInputs;
    if (samples != m_requestedSamples) {
        m_requestedSamples = samples;

        // A multisample texture has its own caps, per kind of format, below
        // GL_MAX_SAMPLES on some drivers. Asked only when the request changes.
        const int32_t colorCap = gl.integer(GL_MAX_COLOR_TEXTURE_SAMPLES);
        const int32_t depthCap = gl.integer(GL_MAX_DEPTH_TEXTURE_SAMPLES);
        const uint32_t cap = static_cast<uint32_t>(std::max(std::min(colorCap, depthCap), 1));

        const uint32_t clamped = std::clamp(samples, 1u, cap);
        if (clamped != m_samples) {
            m_samples = clamped;
            m_width   = 0;  // force a rebuild below, even at unchanged dimensions
        }
    }
    const bool rebuild = width != m_width || height != m_height;
    resize(width, height);
    if (!inputsChange || rebuild || m_width == 0) return;

    // The two images alone gained or dropped: the rest are as they were.
    for (const Attachment input : {Attachment::ReflectWeight, Attachment::ReflectEnv}) {
        detach(input);
        if (carries(input)) allocate(input);
    }
    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u, %u samples)", width, height, m_samples);
    }
}

void GLTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    for (size_t i = 0; i < ATTACHMENT_COUNT; ++i) {
        const Attachment attachment = static_cast<Attachment>(i);
        detach(attachment);
        if (carries(attachment)) allocate(attachment);
    }

    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u, %u samples)", width, height, m_samples);
    }
}

void GLTarget::allocate(Attachment attachment) {
    AttachmentSpec spec = specOf(attachment);
    if (attachment == Attachment::Color) spec.format = m_colorFormat;

    // Multisampled, it is read a sample at a time, so the resolve weighs each rather than a
    // blit averaging them; fixed sample locations, as every attachment of one framebuffer
    // must agree on them.
    using MinFilter = Vkm::GL::TextureMinFilter;
    using MagFilter = Vkm::GL::TextureMagFilter;
    const MinFilter min = spec.linear ? MinFilter::Linear : MinFilter::Nearest;
    const MagFilter mag = spec.linear ? MagFilter::Linear : MagFilter::Nearest;
    auto params = Vkm::GL::renderTargetParams(m_width, m_height, spec.format, GL_RGBA, min, mag);
    params.samples = m_samples;
    std::unique_ptr<Vkm::GL::Texture2D>& slot = m_textures[static_cast<size_t>(attachment)];
    slot = std::make_unique<Vkm::GL::Texture2D>(spec.name, params);
    m_fbo.attachTexture(pointOf(attachment), slot->getID());
}

void GLTarget::bind(const Vkm::GL::Context& gl) {
    const GLenum buffers[1] = { pointOf(Attachment::Color) };
    bindDrawing(gl, buffers, 1);
}

void GLTarget::bindGBufferPass(const Vkm::GL::Context& gl) {
    const GLenum buffers[2] = { GL_NONE, pointOf(Attachment::GBuffer) };
    bindDrawing(gl, buffers, 2);
}

void GLTarget::bindForwardPass(const Vkm::GL::Context& gl, bool reflecting) {
    if (reflecting && hasReflectInputs()) {
        // Indexed by fragment output location, the G-buffer's left untouched.
        const GLenum buffers[4] = {
            pointOf(Attachment::Color),
            GL_NONE,
            pointOf(Attachment::ReflectWeight),
            pointOf(Attachment::ReflectEnv)
        };
        bindDrawing(gl, buffers, 4);
    } else {
        bind(gl);
    }
}

void GLTarget::clearForFrame(const Vkm::GL::Context& gl) {
    if (hasReflectInputs()) {
        // The reflection inputs too: a pixel nothing lit - the sky - has to
        // read as reflecting nothing.
        const GLenum buffers[4] = {
            pointOf(Attachment::Color),
            pointOf(Attachment::GBuffer),
            pointOf(Attachment::ReflectWeight),
            pointOf(Attachment::ReflectEnv)
        };
        bindDrawing(gl, buffers, 4);
    } else if (m_layout == Layout::Scene) {
        const GLenum buffers[2] = { pointOf(Attachment::Color), pointOf(Attachment::GBuffer) };
        bindDrawing(gl, buffers, 2);
    } else {
        bind(gl);
    }
    gl.clear(true, true, false, GLDepth::EYE_CLEAR);
}

void GLTarget::bindDrawing(const Vkm::GL::Context& gl, const GLenum* buffers, int count) {
    bindDrawBuffers(buffers, count);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::bindDrawBuffers(const GLenum* buffers, int count) {
    m_fbo.bind();
    m_fbo.setDrawBuffers(buffers, count);
}

void GLTarget::bindTexture(Attachment attachment, uint32_t slot) const {
    if (const Vkm::GL::Texture2D* tex = texture(attachment)) tex->bindSlot(slot);
}

void GLTarget::bindImage(Attachment attachment, uint32_t unit, GLenum access) const {
    const Vkm::GL::Texture2D* tex = texture(attachment);
    if (tex && !tex->multisampled()) tex->bindImage(unit, access);
}

void GLTarget::blitColorFrom(const GLTarget& src) {
    // Color 0 alone, so the blit never lands in the G-buffer.
    const GLenum buffers[1] = { pointOf(Attachment::Color) };
    bindDrawBuffers(buffers, 1);
    Vkm::GL::FrameBuffer::blit(
        src.m_fbo.getID(),
        m_fbo.getID(),
        0,
        0,
        static_cast<int32_t>(m_width),
        static_cast<int32_t>(m_height),
        0,
        0,
        static_cast<int32_t>(m_width),
        static_cast<int32_t>(m_height),
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );
}

void GLTarget::blitDepthFrom(const GLTarget& src) {
    Vkm::GL::FrameBuffer::blit(
        src.m_fbo.getID(),
        m_fbo.getID(),
        0,
        0,
        static_cast<int32_t>(m_width),
        static_cast<int32_t>(m_height),
        0,
        0,
        static_cast<int32_t>(m_width),
        static_cast<int32_t>(m_height),
        GL_DEPTH_BUFFER_BIT,
        GL_NEAREST
    );
}

} // namespace Vkm::Engine
