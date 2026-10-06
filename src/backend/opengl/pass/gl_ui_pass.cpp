#include "pass/gl_ui_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_frame_buffer.h"
#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_vertex_buffer_layout.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "convention/gl_format_conversion.h"
#include "system/render/render_view.h"
#include "resource/asset/texture_asset.h"
#include "system/ui/ui_draw_data.h"

namespace Vkm::Engine {

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
}

GLUIPass::~GLUIPass() = default;

void GLUIPass::ensureCapacity(uint32_t vertexCount) {
    if (vertexCount <= m_capacity) return;

    // Grow geometrically so a steadily busier UI does not reallocate every frame.
    uint32_t capacity = m_capacity ? m_capacity : 256;
    while (capacity < vertexCount) capacity *= 2;

    m_vbo = std::make_unique<Vkm::GL::VertexBuffer>(
        nullptr,
        capacity * static_cast<uint32_t>(sizeof(UIVertex)),
        GL_DYNAMIC_DRAW
    );

    static_assert(
        sizeof(UIVertex) == (2 + 2 + 4 + 4 + 4 + 1) * sizeof(float),
        "UIVertex no longer matches the layout pushed below"
    );
    Vkm::GL::VertexBufferLayout layout;
    layout.push<float>(2);  // pos   -> location 0
    layout.push<float>(2);  // uv    -> location 1
    layout.push<float>(4);  // color -> location 2
    layout.push<float>(4);  // shape -> location 3: quad size, corner radius, border width
    layout.push<float>(4);  // border colour -> location 4
    layout.push<float>(1);  // image         -> location 5: whether a solid samples its run's image
    m_vao->addBuffer(*m_vbo, layout);

    m_capacity = capacity;
}

void GLUIPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (!view.ui || view.ui->vertices.empty() || view.ui->commands.empty()) return;
    const UIDrawData& ui = *view.ui;

    ensureCapacity(static_cast<uint32_t>(ui.vertices.size()));
    m_vbo->update(ui.vertices.data(), static_cast<uint32_t>(ui.vertices.size() * sizeof(UIVertex)));

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
