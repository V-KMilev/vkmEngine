#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_target.h"

#include <algorithm>
#include <iterator>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "gl_error_handle.h"

#include "convention/gl_bindings.h"

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

// Indexed by GLTarget::Attachment. HDR colour filters linearly; depth and the data images are
// read texel for texel. The colour format here is only the default GLTarget's constructor takes.
constexpr AttachmentSpec SPECS[] = {
    {GL_COLOR_ATTACHMENT0 + Out::COLOR,          GL_RGBA16F,           true,  "target_color"},
    {GL_DEPTH_ATTACHMENT,                        GL_DEPTH_COMPONENT24, false, "target_depth"},
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

/**
 * @brief One attachment's immutable storage.
 *
 * GL_TEXTURE_2D_MULTISAMPLE when multisampled: sampleable, so the resolve reads each sample
 * rather than a blit averaging them.
 */
class GLTarget::Texture {
    public:
        Texture(const AttachmentSpec& spec, uint32_t width, uint32_t height, uint32_t samples)
            : m_target(samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D)
            , m_format(spec.format) {
            VKM_GL_CHECK(glGenTextures(1, &m_id));
            VKM_GL_CHECK(glBindTexture(m_target, m_id));
            const GLsizei w = static_cast<GLsizei>(width);
            const GLsizei h = static_cast<GLsizei>(height);
            if (samples > 1) {
                // Fixed sample locations: every attachment of one framebuffer
                // must agree on them, and the resolve reads sample i of each as
                // the same point of the pixel.
                VKM_GL_CHECK(
                    glTexStorage2DMultisample(
                        m_target,
                        static_cast<GLsizei>(samples),
                        spec.format,
                        w,
                        h,
                        GL_TRUE
                    )
                );
            } else {
                VKM_GL_CHECK(glTexStorage2D(m_target, 1, spec.format, w, h));
                const GLint filter = spec.linear ? GL_LINEAR : GL_NEAREST;
                VKM_GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, filter));
                VKM_GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, filter));
                VKM_GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
                VKM_GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            }
            VKM_GL_CHECK(glObjectLabel(GL_TEXTURE, m_id, -1, spec.name));
            VKM_GL_CHECK(glBindTexture(m_target, 0));
        }

        ~Texture() {
            if (m_id != 0) VKM_GL_CHECK(glDeleteTextures(1, &m_id));
        }

        Texture(const Texture& other) = delete;
        Texture& operator=(const Texture& other) = delete;

        Texture(Texture && other) = delete;
        Texture& operator=(Texture && other) = delete;

    public:
        void bind(uint32_t slot) const {
            VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + slot));
            VKM_GL_CHECK(glBindTexture(m_target, m_id));
        }

        void bindImage(uint32_t unit, GLenum access) const {
            VKM_GL_CHECK(glBindImageTexture(unit, m_id, 0, GL_FALSE, 0, access, m_format));
        }

        GLenum target() const { return m_target; }
        GLuint id() const     { return m_id; }

    private:
        GLenum m_target;
        GLenum m_format;
        GLuint m_id = 0;
};

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

const GLTarget::Texture* GLTarget::texture(Attachment attachment) const {
    return m_textures[static_cast<size_t>(attachment)].get();
}

void GLTarget::release() {
    // Deleting a texture detaches it only from the bound framebuffer; attached to another, its
    // storage lives on. So attachments are let go explicitly, keeping the FBO for the next
    // resize. A target holding nothing binds nothing, so this is cheap every frame.
    const auto held = [](const std::unique_ptr<Texture>& texture) { return texture != nullptr; };
    if (std::none_of(m_textures.begin(), m_textures.end(), held)) return;

    m_fbo.bind();
    for (size_t i = 0; i < ATTACHMENT_COUNT; ++i) detach(static_cast<Attachment>(i));
    m_fbo.unbind();
    m_width  = 0;
    m_height = 0;
}

void GLTarget::detach(Attachment attachment) {
    std::unique_ptr<Texture>& slot = m_textures[static_cast<size_t>(attachment)];
    if (!slot) return;
    VKM_GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, pointOf(attachment), slot->target(), 0, 0));
    slot.reset();
}

void GLTarget::resize(uint32_t width, uint32_t height, uint32_t samples, bool reflectInputs) {
    if (reflectInputs != m_reflectInputs) {
        m_reflectInputs = reflectInputs;
        m_width         = 0;  // rebuild, gaining or dropping the two images
    }
    if (samples != m_requestedSamples) {
        m_requestedSamples = samples;

        // A multisample texture has its own caps, per kind of format, below
        // GL_MAX_SAMPLES on some drivers. Asked only when the request changes.
        GLint colorCap = 1;
        GLint depthCap = 1;
        VKM_GL_CHECK(glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &colorCap));
        VKM_GL_CHECK(glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &depthCap));
        const uint32_t cap = static_cast<uint32_t>(std::max(std::min(colorCap, depthCap), 1));

        const uint32_t clamped = std::clamp(samples, 1u, cap);
        if (clamped != m_samples) {
            m_samples = clamped;
            m_width   = 0;  // force a rebuild below, even at unchanged dimensions
        }
    }
    resize(width, height);
}

void GLTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    m_fbo.bind();
    for (size_t i = 0; i < ATTACHMENT_COUNT; ++i) {
        const Attachment attachment = static_cast<Attachment>(i);
        detach(attachment);
        if (!carries(attachment)) continue;

        AttachmentSpec spec = specOf(attachment);
        if (attachment == Attachment::Color) spec.format = m_colorFormat;

        std::unique_ptr<Texture>& slot = m_textures[i];
        slot = std::make_unique<Texture>(spec, width, height, m_samples);
        VKM_GL_CHECK(
            glFramebufferTexture2D(GL_FRAMEBUFFER, pointOf(attachment), slot->target(), slot->id(), 0)
        );
    }

    if (!m_fbo.isComplete()) {
        LOG_ERROR("GLTarget framebuffer incomplete (%ux%u, %u samples)", width, height, m_samples);
    }
    m_fbo.unbind();
}

void GLTarget::bind(const Vkm::GL::Context& gl) {
    const GLenum buffers[1] = { pointOf(Attachment::Color) };
    bindDrawing(gl, buffers, 1);
}

void GLTarget::bindGBufferPass(const Vkm::GL::Context& gl) {
    const GLenum buffers[2] = { GL_NONE, pointOf(Attachment::GBuffer) };
    bindDrawing(gl, buffers, 2);
}

void GLTarget::bindForwardPass(const Vkm::GL::Context& gl) {
    if (hasReflectInputs()) {
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
    gl.clear(true, true, false);
}

void GLTarget::bindDrawing(const Vkm::GL::Context& gl, const GLenum* buffers, int count) {
    bindDrawBuffers(buffers, count);
    gl.setViewport(0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
}

void GLTarget::bindDrawBuffers(const GLenum* buffers, int count) {
    m_fbo.bind();
    const bool same = count == m_drawBufferCount
        && std::equal(buffers, buffers + count, m_drawBuffers.begin());
    if (same) return;
    VKM_GL_CHECK(glDrawBuffers(count, buffers));
    std::copy(buffers, buffers + count, m_drawBuffers.begin());
    m_drawBufferCount = count;
}

void GLTarget::bindTexture(Attachment attachment, uint32_t slot) const {
    if (const Texture* tex = texture(attachment)) tex->bind(slot);
}

void GLTarget::bindImage(Attachment attachment, uint32_t unit, GLenum access) const {
    const Texture* tex = texture(attachment);
    if (tex && m_samples <= 1) tex->bindImage(unit, access);
}

void GLTarget::blitColorFrom(const GLTarget& src) {
    // Colour 0 alone, so the blit never lands in the G-buffer. The bind is GL_FRAMEBUFFER (read
    // and draw), so the read bind must come after it.
    const GLenum buffers[1] = { pointOf(Attachment::Color) };
    bindDrawBuffers(buffers, 1);
    src.m_fbo.bind(GL_READ_FRAMEBUFFER);
    Vkm::GL::FrameBuffer::blit(
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

} // namespace Vkm::Engine
