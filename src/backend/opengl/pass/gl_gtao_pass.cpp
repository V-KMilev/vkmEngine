#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_gtao_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"

#include "gl_screen_triangle.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLGTAOPass::GLGTAOPass(ShaderHandle shader)
    : RenderPass("GLGTAOPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLGTAOPass::~GLGTAOPass() = default;

bool GLGTAOPass::enabledForView(const RenderView& view) const {
    // Skip the fullscreen AO pass (and, via the prepass gate, the G-buffer
    // fill it feeds on) whenever AO is off. ao.enabled is already forced
    // false for wireframe / diagnostic frames in RenderSystem::buildView.
    return isEnabled() && view.environment.ao.enabled;
}

void GLGTAOPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLGBuffer is owned and resized by GLBackend.
}

void GLGTAOPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLGTAOPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl      = static_cast<GLBackend&>(backend);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    if (!gbuffer.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    gbuffer.bindAO();

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);

    shader->bind();
    shader->setUniform1f("u_radius",    view.environment.ao.radius);
    shader->setUniform1f("u_intensity", view.environment.ao.intensity);
    shader->setUniform1f("u_bias",      0.02f);
    shader->setUniform1f("u_power",     1.5f);
    shader->setUniform1f("u_proj11",    view.camera.projection[1][1]);

    gbuffer.bindNormal(0);
    gbuffer.bindPosition(1);

    m_screenTri->draw();

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
