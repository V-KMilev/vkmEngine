#include "pass/gl_loading_bar_pass.h"

#include <algorithm>
#include <cmath>

#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_screen_triangle.h"
#include "system/loading/loading_frame.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// The fill eases toward the progress at this rate, a second, so a step does not jump.
constexpr float FILL_RATE = 5.0f;

} // namespace

GLLoadingBarPass::GLLoadingBarPass()
    : m_bar("shaders/loading/bar")
{}

GLLoadingBarPass::~GLLoadingBarPass() = default;

void GLLoadingBarPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const bool shows = view.loadingBackdropShows();
    const bool fresh = shows && !m_shown;
    m_shown = shows;
    if (!shows) return;

    const float seconds = fresh ? 0.0f : view.frameSeconds;
    m_time = fresh ? 0.0f : m_time + seconds;

    // Eased toward the progress, never past it; a new load's smaller progress starts it again.
    const float progress = std::clamp(view.loadingProgress, 0.0f, 1.0f);
    if (fresh || progress < m_fill) m_fill = progress;
    m_fill += (progress - m_fill) * (1.0f - std::exp(-FILL_RATE * seconds));

    const float width  = static_cast<float>(std::max(view.viewportWidth, 1u));
    const float height = static_cast<float>(std::max(view.viewportHeight, 1u));
    const LoadingBarRect bar = loadingBar(width, height);

    bindBackbufferViewport(ctx);
    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_bar.bind();
    m_bar.setUniform1f("u_opacity", view.loadingOpacity * view.loadingContent);
    m_bar.setUniform1f("u_time", m_time);
    m_bar.setUniform2f("u_size", width, height);
    m_bar.setUniform4f(
        "u_bar",
        bar.x + 0.5f * bar.width,
        height - (bar.y + 0.5f * bar.height),
        0.5f * bar.width,
        0.5f * bar.height
    );
    m_bar.setUniform1f("u_fill", m_fill);
    m_bar.setUniform1f("u_scale", height / LOADING_REFERENCE_HEIGHT);
    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
