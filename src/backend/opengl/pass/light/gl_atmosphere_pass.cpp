#include "pass/light/gl_atmosphere_pass.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/compute_shader.h"

#include "pass/gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "storage/light/gl_atmosphere.h"
#include "ecs/settings/environment.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLAtmospherePass::GLAtmospherePass()
    : m_skyView("shaders/opengl/light/atmosphere/sky_view")
    , m_aerialPerspective("shaders/opengl/light/atmosphere/aerial_perspective") {}

GLAtmospherePass::~GLAtmospherePass() = default;

void GLAtmospherePass::execute(GLFrameContext& ctx) {
    if (!ctx.sky) return;
    const SkyParams& sky = *ctx.sky;
    const SkySettings& settings = ctx.view.environment->sky;

    ctx.atmosphere.createView();
    ctx.atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);
    ctx.atmosphere.bindMultiScattering(GLBindings::BakeTextureSlots::MULTISCATTERING);
    namespace Groups = GLBindings::ComputeGroups;

    if (settings.showSkybox) {
        m_skyView.bind();
        GLAtmosphere::setSky(m_skyView, sky, sky.sunIlluminance);
        m_skyView.setUniform1f("u_sunZenithCos", sky.sunDir.y);
        ctx.atmosphere.bindSkyViewImage(0, GL_WRITE_ONLY);
        m_skyView.dispatch(
            Groups::covering(GLAtmosphere::SKY_VIEW_WIDTH, Groups::IMAGE),
            Groups::covering(GLAtmosphere::SKY_VIEW_HEIGHT, Groups::IMAGE)
        );
        ctx.skyViewReady = true;
    }

    // The skybox draws the sky at its intensity, so the air a surface fades into does too.
    const float depth = std::min(ctx.view.camera.zFar, GLAtmosphere::AERIAL_PERSPECTIVE_REACH);
    ctx.atmosphere.setAerialDepth(depth);
    m_aerialPerspective.bind();
    GLAtmosphere::setSky(m_aerialPerspective, sky, sky.sunIlluminance * settings.intensity);
    m_aerialPerspective.setUniform3fv("u_sunDir", sky.sunDir);
    m_aerialPerspective.setUniform1f("u_airDepth", depth);
    ctx.atmosphere.bindAerialPerspectiveImage(0, GL_WRITE_ONLY);
    const uint32_t groups = Groups::covering(GLAtmosphere::AERIAL_PERSPECTIVE_SIZE, Groups::IMAGE);
    m_aerialPerspective.dispatch(groups, groups, GLAtmosphere::AERIAL_PERSPECTIVE_SIZE);
    ctx.aerialPerspectiveReady = true;

    // Order the writes before the passes that sample them.
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

} // namespace Vkm::Engine
