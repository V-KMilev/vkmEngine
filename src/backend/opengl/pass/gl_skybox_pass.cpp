#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_skybox_pass.h"

#include <cmath>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_ibl.h"
#include "data/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "generator/mesh_generators.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLSkyboxPass::GLSkyboxPass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/skybox"))
    , m_cube(std::make_unique<GLMesh>(generateCube())) {}

GLSkyboxPass::~GLSkyboxPass() = default;

void GLSkyboxPass::execute(GLFrameContext& ctx) {
    if (!ctx.ibl.isReady()) return;

    const RenderView& view = ctx.view;
    if (!view.environment.sky.showSkybox) return;

    // The vertex shader forces z = w, which is what puts the cube at the far plane.
    ctx.sceneRender.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);   // viewed from inside the cube

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_view", view.camera.view);
    m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    m_shader->setUniform1f("u_iblIntensity", view.environment.sky.intensity);

    // Analytic sun/moon discs and stars, for the procedural sky only - an HDR skybox
    // already has its own. Each disc fades over its outer 20% for a soft limb, and the
    // shader reads the sun's elevation to decide how much of night to apply.
    const Environment& env = view.environment;
    m_shader->setUniform1i("u_hasSun", env.sky.procedural ? 1 : 0);
    if (env.sky.procedural) {
        const float r = env.sky.sunAngularRadius;
        m_shader->setUniform3fv("u_sunDir", ctx.sunDir);
        m_shader->setUniform1f("u_sunCosOuter", std::cos(r));
        m_shader->setUniform1f("u_sunCosInner", std::cos(r * 0.8f));
        m_shader->setUniform1f("u_sunDiscIntensity", env.sky.sunDiscIntensity);

        const float mr = env.night.moonAngularRadius;
        m_shader->setUniform3fv("u_moonDir", env.moonDirection());
        m_shader->setUniform1f("u_moonCosOuter", std::cos(mr));
        m_shader->setUniform1f("u_moonCosInner", std::cos(mr * 0.8f));
        m_shader->setUniform1f("u_moonIntensity", env.night.moonIntensity);
        m_shader->setUniform1f("u_starIntensity", env.night.starIntensity);
        m_shader->setUniform1f("u_starDensity", env.night.starDensity);
    }

    ctx.ibl.bindEnvCube(GLBindings::IBLTextureSlots::ENV_CUBE);
    m_cube->draw();

    // Restore the engine-default depth state so nothing downstream inherits this
    // pass's no-write background fill.
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(true);
}

} // namespace Vkm::Engine
