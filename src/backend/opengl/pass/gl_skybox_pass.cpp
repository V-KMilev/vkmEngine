#include "pass/gl_skybox_pass.h"

#include <cmath>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "storage/gl_ibl.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"
#include "system/sky/atmosphere.h"
#include "ecs/component/render/camera.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The projection the sky is drawn through.
 *
 * An orthographic camera's parallel rays would see one direction of sky across the whole
 * view, so it takes a default camera's field of view at its own aspect instead.
 *
 * @param camera The view's camera.
 * @return @p camera's projection when it is perspective.
 */
glm::mat4 skyProjection(const CameraData& camera) {
    const glm::mat4& projection = camera.projection;
    if (projection[3][3] == 0.0f) return projection;
    const float aspect = projection[1][1] / projection[0][0];
    return glm::perspective(Camera{}.fovY, aspect, camera.zNear, camera.zFar);
}

} // namespace

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
    m_shader.setUniformMatrix4fv("u_skyProjection", skyProjection(view.camera));
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
