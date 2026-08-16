#define VKM_LOG_CATEGORY "BACKEND::GL"

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
#include "texture/gl_texture.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "system/render/render_view.h"
#include "system/ui/ui_draw_data.h"

namespace Engine {

GLUIPass::GLUIPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/ui"))
    , m_vao(std::make_unique<Core::VertexArray>()) {}

GLUIPass::~GLUIPass() = default;

void GLUIPass::ensureCapacity(uint32_t vertexCount) {
    if (vertexCount <= m_capacity) return;

    // Grow geometrically so a steadily busier UI does not reallocate every frame.
    uint32_t capacity = m_capacity ? m_capacity : 256;
    while (capacity < vertexCount) capacity *= 2;

    m_vbo = std::make_unique<Core::VertexBuffer>(
        nullptr, capacity * static_cast<uint32_t>(sizeof(UIVertex)), GL_DYNAMIC_DRAW);

    Core::VertexBufferLayout layout;
    layout.push<float>(2);  // pos   -> location 0
    layout.push<float>(2);  // uv    -> location 1
    layout.push<float>(4);  // color -> location 2
    m_vao->addBuffer(*m_vbo, layout);

    m_capacity = capacity;
}

void GLUIPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const UIDrawData& ui   = view.ui;
    if (ui.vertices.empty() || ui.commands.empty()) return;

    ensureCapacity(static_cast<uint32_t>(ui.vertices.size()));
    m_vbo->update(
        ui.vertices.data(),
        static_cast<uint32_t>(ui.vertices.size() * sizeof(UIVertex)));

    // Draw into the same backbuffer rect Composite resolved to.
    bindBackbufferViewport(ctx);

    ctx.gl.setDepthTest(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Pixel space (top-left origin) -> clip space over the viewport rect.
    const glm::mat4 proj = glm::ortho(
        0.0f, static_cast<float>(view.viewportWidth),
        static_cast<float>(view.viewportHeight), 0.0f);

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_proj", proj);
    m_shader->setUniform1i("u_tex", 0);

    m_vao->bind();
    for (const UIDrawCmd& cmd : ui.commands) {
        // Solid fills ignore the sampler; a text command whose SDF atlas has not
        // reached the GPU yet is skipped, rather than sampling whatever is bound.
        if (cmd.kind == UIDrawKind::Text) {
            const Core::Texture2D* atlas = ctx.resources.getFontAtlas(cmd.font);
            if (!atlas) continue;
            atlas->bindSlot(0);
        }
        m_shader->setUniform1i("u_kind", static_cast<int>(cmd.kind));
        m_vao->drawArrays(
            GL_TRIANGLES,
            static_cast<int32_t>(cmd.firstVertex),
            static_cast<int32_t>(cmd.vertexCount));
    }
}

} // namespace Engine
