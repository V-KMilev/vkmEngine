#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_skybox_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_ibl.h"

#include "generator/mesh_generators.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLSkyboxPass::GLSkyboxPass(ShaderHandle shader)
    : GLRenderPass("GLSkyboxPass")
    , m_shader(shader)
    , m_cube(std::make_unique<GLMesh>(generateCube()))
{
}

void GLSkyboxPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for the skybox pass.
}

bool GLSkyboxPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.skybox.enabled;
}

void GLSkyboxPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& glView = gl.getView();
    auto& ibl    = *rg.resource<GLIBL>(RGResource::IBL);
    auto& hdrT   = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);

    if (!ibl.isReady()) return;  // no baked environment -> nothing to draw

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    // Draw into the same HDR target as the scene (no clear).
    hdrT.bindForRender();

    auto&        ctx       = gl.getContext();
    const GLenum prevFunc  = ctx.getDepthFunc();
    const bool   prevWrite = ctx.isDepthWriteEnabled();
    const bool   prevCull  = ctx.isFaceCullingEnabled();

    ctx.setDepthTest(true);
    ctx.setDepthWrite(false);
    ctx.setDepthFunc(GL_LEQUAL);   // skybox depth is forced to 1.0
    ctx.setFaceCulling(false);     // viewed from inside the cube

    shader->bind();

    shader->setUniformMatrix4fv("u_view", view.camera.view);
    shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    shader->setUniform1f("u_iblIntensity", view.environment.ibl.intensity);

    ibl.bindEnvCube(0);

    m_cube->draw(GL_TRIANGLES);

    ctx.setDepthFunc(prevFunc);
    ctx.setDepthWrite(prevWrite);
    ctx.setFaceCulling(prevCull);
}

} // namespace Engine
