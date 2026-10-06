#include "pass/gl_skybox_pass.h"

#include <cmath>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "storage/gl_ibl.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"
#include "system/sky/atmosphere.h"

namespace Vkm::Engine {

GLSkyboxPass::GLSkyboxPass()
    : m_shader("shaders/skybox") {}

GLSkyboxPass::~GLSkyboxPass() = default;

void GLSkyboxPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;

    // With no sky to show the background stays the prepass's black - unless
    // there is fog, which lies in front of it all the same.
    const bool sky = ctx.ibl.isReady() && view.environment.sky.showSkybox;
    if (!sky && !ctx.fogReady) return;

    // The vertex shader forces z = w, putting the cube at the far plane, where the LEQUAL
    // GLBackend::render sets passes it. Culling stays off: the cube is seen from inside.
    ctx.sceneRender.bind(ctx.gl);
    ctx.gl.setDepthWrite(false);

    m_shader.bind();
    bindFog(ctx, m_shader);
    m_shader.setUniform1i("u_hasSky", sky ? 1 : 0);
    m_shader.setUniform1f("u_iblIntensity", view.environment.sky.intensity);

    // Analytic sun/moon discs and stars, for the procedural sky only; an HDR skybox has its own.
    // Each disc fades over its outer 20%; the sun's elevation sets how much night applies.
    const Environment& env = view.environment;
    m_shader.setUniform1i("u_hasSun", env.sky.procedural ? 1 : 0);
    if (env.sky.procedural) {
        const float r = env.sky.sunAngularRadius;
        m_shader.setUniform3fv("u_sunDir", ctx.sunDir);
        m_shader.setUniform1f("u_sunCosOuter", std::cos(r));
        m_shader.setUniform1f("u_sunCosInner", std::cos(r * 0.8f));
        m_shader.setUniform1f("u_sunDiscIntensity", env.sky.sunDiscIntensity);
        m_shader.setUniform3fv("u_sunColor", Atmosphere::sunlight(env.sky));

        const float mr = env.night.moonAngularRadius;
        m_shader.setUniform3fv("u_moonDir", env.moonDirection());
        m_shader.setUniform1f("u_moonCosOuter", std::cos(mr));
        m_shader.setUniform1f("u_moonCosInner", std::cos(mr * 0.8f));
        m_shader.setUniform1f("u_moonIntensity", env.night.moonIntensity);
        m_shader.setUniform1f("u_starIntensity", env.night.starIntensity);
        m_shader.setUniform1f("u_starDensity", env.night.starDensity);
    }

    if (sky) ctx.ibl.bindEnvCube(GLBindings::IBLTextureSlots::ENV_CUBE);
    ctx.unitCube.draw();
}

} // namespace Vkm::Engine
