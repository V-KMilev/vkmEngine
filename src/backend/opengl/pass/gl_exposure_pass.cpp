#include "gl_exposure_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"

#include "core/gl_backend.h"
#include "core/gl_hdr_target.h"
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
    return isEnabled() && view.environment.autoExposure && !view.environment.wireframe;
}

GLExposurePass::GLExposurePass(ShaderHandle lumShader, ShaderHandle adaptShader)
    : RenderPass("GLExposurePass")
    , m_lumShader(lumShader)
    , m_adaptShader(adaptShader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLExposurePass::~GLExposurePass() = default;

void GLExposurePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLAutoExposure targets are fixed-size and viewport-independent.
}

void GLExposurePass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLExposurePass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl  = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLHdrTarget>(RGResource::SceneHDR);
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
    adapt->setUniform1f("u_speed", view.environment.exposureSpeed);

    ae.bindLum(0);
    ae.bindAdapted(1);

    ae.attachAdaptWrite();
    m_screenTri->emit();

    ae.swap();

    m_screenTri->unbind();
    ae.unbindFbo();

    ctx.setViewport(0, 0,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight));
    ctx.setDepthTest(true);
}

} // namespace Engine
