#include "pass/overlay/gl_ui_pass.h"

#include <cstddef>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "vkm_opengl/buffer/stream_ring.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/texture.h"
#include "vkm_opengl/vertex/vertex_array.h"

#include "pass/gl_frame_context.h"
#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "asset/gl_texture_conversion.h"
#include "system/render/render_view.h"
#include "resource/asset/texture_asset.h"
#include "core/frame/ui_draw_data.h"

namespace Vkm::Engine {

namespace {

// The vertex buffer binding point the attributes read, a namespace apart from their locations.
constexpr uint32_t QUAD_BINDING = 0;

} // namespace

GLUIPass::GLUIPass()
    : m_shader("shaders/opengl/overlay/ui")
    , m_vao(std::make_unique<Vkm::OpenGL::VertexArray>())
{
    const uint8_t zero = 0;
    Vkm::OpenGL::Texture2DParams params;
    params.width           = 1;
    params.height          = 1;
    params.internalFormat  = GL_R8;
    params.format          = GL_RED;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Vkm::OpenGL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::OpenGL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    params.data            = &zero;
    m_noAtlas              = std::make_unique<Vkm::OpenGL::Texture2D>("ui_no_atlas", params);

    // The quad's format once, an instance a quad; each frame's quads are bound from the frame's stream.
    static_assert(
        sizeof(UIQuad) == (4 + 4 + 4 + 4 + 4 + 3) * sizeof(float),
        "UIQuad no longer matches the attributes below"
    );
    const auto attribute = [this](uint32_t index, int32_t count, uint32_t at) {
        m_vao->setAttribute(index, QUAD_BINDING, count, GL_FLOAT, at);
    };
    namespace Attributes = GLBindings::UIAttributes;
    attribute(Attributes::RECT, 4, offsetof(UIQuad, rect));
    attribute(Attributes::UV, 4, offsetof(UIQuad, uv));
    attribute(Attributes::TOP, 4, offsetof(UIQuad, top));
    attribute(Attributes::BOTTOM, 4, offsetof(UIQuad, bottom));
    attribute(Attributes::BORDER, 4, offsetof(UIQuad, border));
    attribute(Attributes::STYLE, 3, offsetof(UIQuad, style));
    m_vao->setDivisor(QUAD_BINDING, 1);
}

GLUIPass::~GLUIPass() = default;

void GLUIPass::execute(GLFrameContext& ctx) {
    draw(ctx, ctx.view.ui);
}

void GLUIPass::draw(GLFrameContext& ctx, const UIDrawData* list) {
    if (!list || list->quads.empty() || list->commands.empty()) return;
    const RenderView& view = ctx.view;
    const UIDrawData& ui   = *list;

    const auto bytes = static_cast<uint32_t>(ui.quads.size() * sizeof(UIQuad));
    const Vkm::OpenGL::BufferSpan quads = ctx.mirror.stream().write(ui.quads.data(), bytes);

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
        const Vkm::OpenGL::Texture2D* atlas = ctx.mirror.getFontAtlas(cmd.font);
        (atlas ? *atlas : *m_noAtlas).bindSlot(GLBindings::OverlayTextureSlots::UI_ATLAS);
        bindImage(ctx, cmd.image);
        setScissorRect(ctx, cmd.clip);
        // The run's quads from its first, a corner of two triangles each vertex (shaders/opengl/overlay/ui).
        const auto at  = static_cast<uint32_t>(cmd.firstQuad * sizeof(UIQuad));
        const auto run = static_cast<uint32_t>(cmd.quadCount * sizeof(UIQuad));
        m_vao->setVertexBuffer(QUAD_BINDING, {quads.buffer, quads.offset + at, run}, sizeof(UIQuad));
        m_vao->drawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<int32_t>(cmd.quadCount));
    }
}

void GLUIPass::bindImage(GLFrameContext& ctx, const TextureHandle& image) const {
    // A run with no image still binds something deliberate. A loading or failed image shows
    // the missing-texture checker, as a material's map would.
    const Vkm::OpenGL::Texture2D* texture = image ? ctx.mirror.getTexture(image) : m_noAtlas.get();
    if (!texture) texture = &ctx.mirror.missingTexture();
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
