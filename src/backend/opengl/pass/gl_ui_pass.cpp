#include "pass/gl_ui_pass.h"

#include <cstddef>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_frame_buffer.h"
#include "gl_vertex_array.h"
#include "gl_error_handle.h"
#include "gl_stream_ring.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "convention/gl_format_conversion.h"
#include "system/render/render_view.h"
#include "resource/asset/texture_asset.h"
#include "system/ui/ui_draw_data.h"

namespace Vkm::Engine {

namespace {

// The vertex buffer binding point the attributes read, a namespace apart from their locations.
constexpr uint32_t VERTEX_BINDING = 0;

} // namespace

GLUIPass::GLUIPass()
    : m_shader("shaders/ui")
    , m_vao(std::make_unique<Vkm::GL::VertexArray>())
{
    const uint8_t zero = 0;
    Vkm::GL::Texture2DParams params;
    params.width           = 1;
    params.height          = 1;
    params.internalFormat  = GL_R8;
    params.format          = GL_RED;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    params.data            = &zero;
    m_noAtlas = std::make_unique<Vkm::GL::Texture2D>("ui_no_atlas", params);

    // The vertex formats once; each frame's vertices are bound from the frame's stream.
    static_assert(
        sizeof(UIVertex) == (2 + 2 + 4 + 4 + 4 + 1) * sizeof(float),
        "UIVertex no longer matches the attributes below"
    );
    const GLuint vao       = m_vao->getID();
    const auto   attribute = [vao](uint32_t index, GLint count, uint32_t at) {
        VKM_GL_CHECK(glEnableVertexArrayAttrib(vao, index));
        VKM_GL_CHECK(glVertexArrayAttribFormat(vao, index, count, GL_FLOAT, GL_FALSE, at));
        VKM_GL_CHECK(glVertexArrayAttribBinding(vao, index, VERTEX_BINDING));
    };
    attribute(0, 2, offsetof(UIVertex, pos));
    attribute(1, 2, offsetof(UIVertex, uv));
    attribute(2, 4, offsetof(UIVertex, color));
    attribute(3, 4, offsetof(UIVertex, shape));   // quad size, corner radius, border width
    attribute(4, 4, offsetof(UIVertex, border));  // border colour
    attribute(5, 1, offsetof(UIVertex, image));   // whether a solid samples its run's image
}

GLUIPass::~GLUIPass() = default;

void GLUIPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (!view.ui || view.ui->vertices.empty() || view.ui->commands.empty()) return;
    const UIDrawData& ui = *view.ui;

    const auto                bytes    = static_cast<uint32_t>(ui.vertices.size() * sizeof(UIVertex));
    const Vkm::GL::BufferSpan vertices = ctx.resources.stream().write(ui.vertices.data(), bytes);
    VKM_GL_CHECK(
        glVertexArrayVertexBuffer(
            m_vao->getID(),
            VERTEX_BINDING,
            vertices.buffer,
            vertices.offset,
            sizeof(UIVertex)
        )
    );

    // Draw into the same backbuffer rect Composite resolved to.
    bindBackbufferViewport(ctx);

    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Pixel space (top-left origin) -> clip space over the viewport rect.
    const glm::mat4 proj = glm::ortho(
        0.0f,
        static_cast<float>(view.viewportWidth),
        static_cast<float>(view.viewportHeight),
        0.0f
    );

    m_shader.bind();
    m_shader.setUniformMatrix4fv("u_proj", proj);

    // On for the whole pass, each command setting its own box (the viewport when unclipped):
    // no per-command branch, no state half-applied between commands.
    ctx.gl.enableScissor(true);

    m_vao->bind();
    for (const UIDrawCmd& cmd : ui.commands) {
        // Text and solids share a run; the vertex says which. An atlas not yet on the GPU reads
        // as no coverage, so its glyphs draw nothing rather than sampling whatever is bound.
        const Vkm::GL::Texture2D* atlas = ctx.resources.getFontAtlas(cmd.font);
        (atlas ? *atlas : *m_noAtlas).bindSlot(GLBindings::OverlayTextureSlots::UI_ATLAS);
        bindImage(ctx, cmd.image);
        setScissorRect(ctx, cmd.clip);
        m_vao->drawArrays(
            GL_TRIANGLES,
            static_cast<int32_t>(cmd.firstVertex),
            static_cast<int32_t>(cmd.vertexCount)
        );
    }

    // Not reset between passes, so this pass turns off what it turned on.
    ctx.gl.enableScissor(false);
}

void GLUIPass::bindImage(GLFrameContext& ctx, const TextureHandle& image) const {
    // A run with no image still binds something deliberate. A loading or failed image shows
    // the missing-texture checker, as a material's map would.
    const Vkm::GL::Texture2D* texture = image ? ctx.resources.getTexture(image) : m_noAtlas.get();
    if (!texture) texture = &ctx.resources.missingTexture();
    texture->bindSlot(GLBindings::OverlayTextureSlots::UI_IMAGE);

    // The UI blends in display space, so a texture stored as sRGB - whose
    // sampling returns linear light - is encoded back before the tint.
    const bool srgb = isSrgbGLFormat(texture->getParams().internalFormat);
    m_shader.setUniform1i("u_imageSrgb", srgb ? 1 : 0);
}

void GLUIPass::setScissorRect(GLFrameContext& ctx, const UIRect& clip) const {
    const RenderView& view = ctx.view;
    // The clip is viewport-local with a top-left origin; the scissor is in the bound viewport's
    // window space. Edges round outward, keeping pixels the clip partly covers.
    const glm::ivec2 lo(glm::floor(clip.pos));
    const glm::ivec2 hi(glm::ceil(clip.max()));
    ctx.gl.setScissor(
        static_cast<int32_t>(view.viewportX) + lo.x,
        backbufferBottom(view, lo.y, hi.y - lo.y),
        hi.x - lo.x,
        hi.y - lo.y
    );
}

} // namespace Vkm::Engine
