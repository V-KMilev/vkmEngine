#include "pass/post/gl_upscale_pass.h"

#include <cmath>
#include <cstdint>

#include "vkm_opengl/core/context.h"

#include "gl_screen_triangle.h"

#include "pass/gl_frame_context.h"
#include "gl_profiler.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// How far RCAS stays under its sharpest, in stops: a quarter stop keeps edges crisp without halos.
constexpr float RCAS_ATTENUATION = 0.25f;

} // namespace

GLUpscalePass::GLUpscalePass()
    : m_easu("shaders/opengl/post/upscale/easu")
    , m_rcas("shaders/opengl/post/upscale/rcas") {}

GLUpscalePass::~GLUpscalePass() = default;

void GLUpscalePass::execute(GLFrameContext& ctx) {
    if (!ctx.upscaleSource) return;

    const RenderView& view = ctx.view;
    m_scaled.resize(view.viewportWidth, view.viewportHeight);
    ctx.gl.setDepthTest(false);
    ctx.screenTri.bind();

    {
        PROFILE_GPU_SCOPE_NAMED("Upscale/EASU");
        m_scaled.bind(ctx.gl);
        m_easu.bind();
        ctx.upscaleSource->bindTexture(GLTarget::Attachment::Color, GLBindings::UpscaleTextureSlots::SOURCE);
        m_easu.setUniform2f(
            "u_outputSize",
            static_cast<float>(view.viewportWidth),
            static_cast<float>(view.viewportHeight)
        );
        ctx.screenTri.emit();
    }
    {
        PROFILE_GPU_SCOPE_NAMED("Upscale/RCAS");
        bindBackbufferViewport(ctx);
        m_rcas.bind();
        m_scaled.bindTexture(GLTarget::Attachment::Color, GLBindings::UpscaleTextureSlots::SOURCE);
        const int32_t left   = static_cast<int32_t>(view.viewportX);
        const int32_t height = static_cast<int32_t>(view.viewportHeight);
        m_rcas.setUniform2i("u_origin", left, backbufferBottom(view, 0, height));
        m_rcas.setUniform1f("u_sharpness", std::exp2(-RCAS_ATTENUATION));
        ctx.screenTri.emit();
    }
}

} // namespace Vkm::Engine
