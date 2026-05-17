#include "gl_skybox_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_ibl.h"

#include "generator/mesh_generators.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLSkyboxPass::GLSkyboxPass(ShaderHandle shader)
    : RenderPass("GLSkyboxPass")
    , m_shader(shader)
    , m_cube(std::make_unique<GLMesh>(generateCube()))
{
}

void GLSkyboxPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Nothing to do for the skybox pass.
}

void GLSkyboxPass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLSkyboxPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl     = static_cast<GLBackend&>(backend);
    auto& glView = gl.getView();
    auto& ibl    = glView.getIBL();

    const bool proceduralSky = view.environment.proceduralSky;
    if (!ibl.isReady() && !proceduralSky) return;  // nothing to draw

    // Direction toward the sun = opposite the first directional light's
    // forward (rotation * +Z, matching GLLights). Default if no sun.
    glm::vec3 sunToward = glm::normalize(glm::vec3(0.3f, 0.7f, 0.2f));
    for (const auto& l : view.lights) {
        if (l.type == LightType::Directional) {
            sunToward = glm::normalize(-(l.rotation * glm::vec3(0.0f, 0.0f, 1.0f)));
            break;
        }
    }

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    // Draw into the same HDR target as the scene (no clear).
    gl.getHdrTarget().bindForRender();

    auto&        ctx       = gl.getContext();
    const GLenum prevFunc  = ctx.getDepthFunc();
    const bool   prevWrite = ctx.isDepthWriteEnabled();
    const bool   prevCull  = ctx.isFaceCullingEnabled();

    ctx.setDepthTest(true);
    ctx.setDepthWrite(false);
    ctx.setDepthFunc(GL_LEQUAL);   // skybox depth is forced to 1.0
    ctx.setFaceCulling(false);     // viewed from inside the cube

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    shader->setUniformMatrix4fv("u_view", view.camera.view);
    shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    shader->setUniform1f("u_iblIntensity", view.environment.iblIntensity);
    shader->setUniform1i("u_proceduralSky", proceduralSky ? 1 : 0);
    shader->setUniform3fv("u_sunDir", sunToward);
    shader->setUniform1f("u_turbidity", view.environment.skyTurbidity);
    shader->setUniform1f("u_skyIntensity", view.environment.skyIntensity);

    if (ibl.isReady()) {
        ibl.bindEnvCube(0);
    }

    m_cube->draw(GL_TRIANGLES);

    ctx.setDepthFunc(prevFunc);
    ctx.setDepthWrite(prevWrite);
    ctx.setFaceCulling(prevCull);
}

} // namespace Engine
