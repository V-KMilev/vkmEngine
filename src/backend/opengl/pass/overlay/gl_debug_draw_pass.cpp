#include "pass/overlay/gl_debug_draw_pass.h"

#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/buffer/stream_ring.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "pass/gl_frame_context.h"
#include "gl_screen_triangle.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDebugDrawPass::GLDebugDrawPass()
    : m_shader("shaders/opengl/overlay/debug_draw") {}

GLDebugDrawPass::~GLDebugDrawPass() = default;

void GLDebugDrawPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const std::vector<DebugLine>* lines = view.lists.debug.lines;
    if (!lines || lines->empty() || !view.hasCamera) return;

    const uint32_t bytes = static_cast<uint32_t>(lines->size() * sizeof(DebugLine));
    const Vkm::OpenGL::BufferSpan uploaded = ctx.mirror.stream().write(lines->data(), bytes);
    Vkm::OpenGL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::DEBUG_LINES, uploaded);

    bindBackbufferViewport(ctx);
    const int32_t height = static_cast<int32_t>(view.viewportHeight);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);

    m_shader.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
    m_shader.setUniform2fv("u_targetSize", glm::vec2(view.viewportWidth, view.viewportHeight));
    m_shader.setUniform2fv("u_targetOrigin", glm::vec2(view.viewportX, backbufferBottom(view, 0, height)));
    m_shader.setUniform1f("u_pixelsPerPoint", view.lists.debug.pointScale);

    ctx.screenTri.bind();
    ctx.screenTri.array().drawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<int32_t>(lines->size()));
    ctx.screenTri.unbind();
}

} // namespace Vkm::Engine
