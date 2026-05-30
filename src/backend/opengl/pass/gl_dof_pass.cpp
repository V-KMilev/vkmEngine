#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_dof_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_post_scratch.h"

#include "gl_screen_triangle.h"
#include "gl_blit.h"
#include "gl_fullscreen_post.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLDofPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.dof.enabled && !view.modeConfig.disablePost;
}

GLDofPass::GLDofPass(ShaderHandle shader)
    : GLRenderPass("GLDofPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLDofPass::~GLDofPass() = default;

void GLDofPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Scratch / HDR / G-buffer are owned and resized by GLBackend.
}

void GLDofPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& scratch = *rg.resource<GLPostScratch>(RGResource::PostScratch);
    if (!hdr.isReady() || !gbuffer.isReady() || !scratch.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    beginFullscreenPost(gl.getContext());

    scratch.bindForRender();

    shader->bind();
    shader->setUniform1f("u_focusDistance", view.environment.dof.focusDistance);
    shader->setUniform1f("u_focusRange",    view.environment.dof.focusRange);
    shader->setUniform1f("u_maxBlur",       view.environment.dof.maxBlur);

    hdr.bindResolvedColor(0);
    gbuffer.bindPosition(1);

    m_screenTri->draw();

    endFullscreenPost(gl.getContext(),
        scratch.fboId(), static_cast<int>(scratch.width()), static_cast<int>(scratch.height()),
        hdr.resolveFboId(), static_cast<int>(hdr.width()), static_cast<int>(hdr.height()));
}

} // namespace Engine
