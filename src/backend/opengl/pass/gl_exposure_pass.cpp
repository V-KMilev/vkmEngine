#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_exposure_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_auto_exposure.h"

#include "gl_screen_triangle.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLExposurePass::enabledForView(const RenderView& view) const {
    // Skip metering entirely when auto-exposure is off; the composite
    // already gates on it. Also off in wireframe (would meter a dark scene
    // and crank exposure on the next non-wireframe frame).
    return isEnabled() && view.environment.exposure.autoExposure && !view.modeConfig.disablePost;
}

GLExposurePass::GLExposurePass(ShaderHandle lumShader, ShaderHandle adaptShader)
    : GLRenderPass("GLExposurePass")
    , m_lumShader(lumShader)
    , m_adaptShader(adaptShader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLExposurePass::~GLExposurePass() = default;

void GLExposurePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLAutoExposure targets are fixed-size and viewport-independent.
}

void GLExposurePass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& ae = *rg.resource<GLAutoExposure>(RGResource::AdaptedLuminance);
    if (!hdr.isReady()) return;

    GLShader* lum   = gl.getView().resolveShader(m_lumShader, resources);
    GLShader* adapt = gl.getView().resolveShader(m_adaptShader, resources);
    if (!lum || !adapt) return;

    ae.createTargets();
    // SceneHDRResolved is produced by the graph's auto MSAA-resolve.

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(false);

    m_screenTri->bind();
    ae.bindFbo();

    // 1. Scene -> log-luminance (mip 0), then reduce to the 1x1 top mip.
    lum->bind();
    hdr.bindResolvedColor(0);
    ae.attachLum();
    m_screenTri->emit();

    ae.reduceLum();

    // 2. Temporal adaptation into the ping-pong 1x1 target.
    adapt->bind();
    adapt->setUniform1f("u_lumMaxLod", static_cast<float>(GLAutoExposure::LUM_MIPS - 1));
    adapt->setUniform1f("u_deltaTime", view.deltaTime);
    adapt->setUniform1f("u_speedBrighten", view.environment.exposure.speedBrighten);
    adapt->setUniform1f("u_speedDarken",   view.environment.exposure.speedDarken);

    ae.bindLum(0);
    ae.bindAdapted(1);

    ae.attachAdaptWrite();
    m_screenTri->emit();

    ae.swap();

    // Mirror the adapted 1x1 luminance back to the CPU for the editor's
    // Exposure EV readout only - no rendering path reads it (the composite
    // samples the adapted-luminance texture on the GPU). readAdapted() is a
    // blocking glGetTexImage, so throttle it instead of stalling the pipeline
    // every frame at max settings; eye adaptation is slow, so a few-Hz editor
    // readout is imperceptible.
    constexpr uint64_t ADAPTED_READBACK_INTERVAL = 8;
    if (rg.frameIndex % ADAPTED_READBACK_INTERVAL == 0) {
        gl.setAdaptedLuminance(ae.readAdapted());
    }

    m_screenTri->unbind();
    ae.unbindFbo();

    ctx.setViewport(0, 0,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight));
    ctx.setDepthTest(true);
}

} // namespace Engine
