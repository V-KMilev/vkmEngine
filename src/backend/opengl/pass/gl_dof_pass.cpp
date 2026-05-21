#include "gl_dof_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"

#include "core/gl_backend.h"
#include "core/gl_hdr_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_post_scratch.h"

#include "gl_screen_triangle.h"
#include "gl_blit.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLDofPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.dof && !view.environment.wireframe;
}

GLDofPass::GLDofPass(ShaderHandle shader)
    : RenderPass("GLDofPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLDofPass::~GLDofPass() = default;

void GLDofPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Scratch / HDR / G-buffer are owned and resized by GLBackend.
}

void GLDofPass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;

    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLDofPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl      = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLHdrTarget>(RGResource::SceneHDR);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& scratch = *rg.resource<GLPostScratch>(RGResource::PostScratch);
    if (!hdr.isReady() || !gbuffer.isReady() || !scratch.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);

    scratch.bindForRender();

    shader->bind();
    shader->setUniform1f("u_focusDistance", view.environment.dofFocusDistance);
    shader->setUniform1f("u_focusRange",    view.environment.dofFocusRange);
    shader->setUniform1f("u_maxBlur",       view.environment.dofMaxBlur);

    hdr.bindResolvedColor(0);
    gbuffer.bindPosition(1);

    m_screenTri->draw();

    Core::blitColor(scratch.fboId(), hdr.resolveFboId(),
        static_cast<int>(scratch.width()), static_cast<int>(scratch.height()),
        static_cast<int>(hdr.width()),     static_cast<int>(hdr.height()), GL_NEAREST);

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
