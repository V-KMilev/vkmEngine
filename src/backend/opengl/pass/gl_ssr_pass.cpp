#include "gl_ssr_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"

#include "gl_screen_triangle.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLSSRPass::enabledForView(const RenderView& view) const {
    // Off in wireframe: SSR reads filled-triangle gbuffer positions and
    // would draw a ghost of the solid mesh between wireframe lines.
    return isEnabled() && view.environment.ssr.enabled && !view.modeConfig.disablePost;
}

GLSSRPass::GLSSRPass(ShaderHandle shader)
    : RenderPass("GLSSRPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLSSRPass::~GLSSRPass() = default;

void GLSSRPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Reuses GLGBuffer / GLSceneTarget, both owned and resized by GLBackend.
}

void GLSSRPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLSSRPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl      = static_cast<GLBackend&>(backend);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    if (!gbuffer.isReady() || !hdr.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    // Sample source = the lit scene so far (opaque + skybox + grid); the
    // graph already resolved SceneHDRResolved before this pass.
    hdr.bindForRender();

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(true);
    ctx.setBlendFunc(GL_ONE, GL_ONE);   // additive: reflections add onto the scene

    shader->bind();
    shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    shader->setUniform1f("u_intensity",   view.environment.ssr.intensity);
    shader->setUniform1f("u_maxDistance", view.environment.ssr.maxDistance);
    shader->setUniform1f("u_thickness",   view.environment.ssr.thickness);

    hdr.bindResolvedColor(0);
    gbuffer.bindNormal(1);
    gbuffer.bindPosition(2);

    m_screenTri->draw();

    ctx.setBlending(false);
    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
