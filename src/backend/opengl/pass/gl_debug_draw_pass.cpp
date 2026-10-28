#include "pass/gl_debug_draw_pass.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_context.h"
#include "gl_shader.h"
#include "gl_stream_ring.h"

#include "gl_frame_context.h"
#include "gl_screen_triangle.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDebugDrawPass::GLDebugDrawPass(Stage stage)
    : m_stage(stage)
    , m_shader("shaders/debug") {}

GLDebugDrawPass::~GLDebugDrawPass() = default;

void GLDebugDrawPass::execute(GLFrameContext& ctx) {
    const RenderView&             view  = ctx.view;
    const std::vector<DebugLine>* lines = view.debug.lines;
    if (!lines || lines->empty() || !view.hasCamera) return;

    const bool glow = m_stage == Stage::Glow;
    if (glow) {
        // A glow is light for the bloom to spread: none while it is off (DebugStyle::glowing).
        if (!view.settings.bloom) return;
        const auto glows = [](const DebugLine& line) { return line.glow > 0.0f; };
        if (std::none_of(lines->begin(), lines->end(), glows)) return;
    }

    if (!ctx.debugLines) {
        const uint32_t bytes = static_cast<uint32_t>(lines->size() * sizeof(DebugLine));
        ctx.debugLines = ctx.resources.stream().write(lines->data(), bytes);
    }
    Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::DEBUG_LINES, ctx.debugLines);

    // Glow lights the scene's pixels, which may be fewer than the window's it is shown at.
    glm::vec2 size(view.viewportWidth, view.viewportHeight);
    glm::vec2 origin(0.0f);
    float     pixelsPerPoint = view.debug.pointScale;
    if (glow) {
        // Blends into the color chain while sampling the geometry target's depth.
        promoteColorChain(ctx);
        ctx.colorSrc->bind(ctx.gl);
        const glm::vec2 scene(view.sceneWidth(), view.sceneHeight());
        pixelsPerPoint *= scene.x / std::max(size.x, 1.0f);
        size = scene;
        ctx.gl.setBlendFunc(GL_ONE, GL_ONE);
    } else {
        bindBackbufferViewport(ctx);
        const int32_t height = static_cast<int32_t>(view.viewportHeight);
        origin = glm::vec2(view.viewportX, backbufferBottom(view, 0, height));
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);

    m_shader.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
    m_shader.setUniform2fv("u_targetSize", size);
    m_shader.setUniform2fv("u_targetOrigin", origin);
    m_shader.setUniform1f("u_pixelsPerPoint", pixelsPerPoint);
    m_shader.setUniform1f("u_seconds", static_cast<float>(view.debug.seconds));
    m_shader.setUniform1i("u_glowStage", glow ? 1 : 0);
    m_shader.setUniform1f("u_unexposed", std::exp2(-ctx.exposure));

    ctx.screenTri.bind();
    ctx.screenTri.array().drawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<int32_t>(lines->size()));
    ctx.screenTri.unbind();
}

} // namespace Vkm::Engine
