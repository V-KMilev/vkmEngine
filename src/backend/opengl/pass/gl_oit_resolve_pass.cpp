#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_oit_resolve_pass.h"

#include <GL/glew.h>

#include "logger.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "debug/profiler_gl.h"
#include "gl_screen_triangle.h"
#include "resource/gl_oit.h"
#include "resource/gl_shader_program.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

bool GLOITResolvePass::enabledForView(const RenderView& view) const {
    return isEnabled()
        && view.environment.transparency.useOIT
        && !view.modeConfig.disablePost
        && !view.modeConfig.forceUnlit;  // diagnostic modes bypass.
}

GLOITResolvePass::GLOITResolvePass(ShaderHandle shader)
    : RenderPass("GLOITResolvePass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLOITResolvePass::~GLOITResolvePass() = default;

void GLOITResolvePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // No viewport-sized state of our own; the OIT target lives in
    // FrameResources and is resized by the graph.
}

void GLOITResolvePass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLOITResolvePass requires OpenGL backend - skipping");
        return;
    }

    auto& gl  = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    GLOIT* oit = rg.resource<GLOIT>(RGResource::OITAccum);
    // OITRevealage shares the same GLOIT object - access for graph tracking.
    (void)rg.resource<GLOIT>(RGResource::OITRevealage);
    if (!oit || !oit->isReady() || !hdr.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& ctx = gl.getContext();

    // Composite back into the MSAA HDR target. The OIT contribution writes
    // the per-pixel transparent color with opacity (1 - revealage); the
    // pre-existing opaque/sky pixels remain where revealage == 1.
    hdr.bindForRender();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(true);
    // glBlendFunc covers all draw buffers - this overrides any per-attachment
    // blendFunci state left over from the OIT forward phase.
    ctx.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->bind();
    oit->bindAccumForReading(0);
    oit->bindRevealageForReading(1);
    if (shader->hasUniform("u_oitAccum"))     shader->setUniform1i("u_oitAccum", 0);
    if (shader->hasUniform("u_oitRevealage")) shader->setUniform1i("u_oitRevealage", 1);

    m_screenTri->bind();
    m_screenTri->emit();
    m_screenTri->unbind();

    // Restore conservative state for the next pass.
    ctx.setBlending(false);
    ctx.setDepthTest(true);
}

} // namespace Engine
