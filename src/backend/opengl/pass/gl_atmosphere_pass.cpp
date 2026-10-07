#include "pass/gl_atmosphere_pass.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "storage/gl_atmosphere.h"
#include "ecs/environment.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLAtmospherePass::GLAtmospherePass()
    : m_skyView("shaders/atmosphere/sky_view")
    , m_aerialPerspective("shaders/atmosphere/aerial_perspective") {}

GLAtmospherePass::~GLAtmospherePass() = default;

void GLAtmospherePass::execute(GLFrameContext& ctx) {
    if (!ctx.sky) return;
    const SkyParams&   sky      = *ctx.sky;
    const SkySettings& settings = ctx.view.environment.sky;
    const bool         drawSky  = settings.showSkybox;
    const bool         drawAir  = settings.aerialPerspective > 0.0f;
    if (!drawSky && !drawAir) return;

    ctx.atmosphere.createView();
    ctx.atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);
    ctx.atmosphere.bindMultiScattering(GLBindings::BakeTextureSlots::MULTISCATTERING);
    namespace Groups = GLBindings::ComputeGroups;

    if (drawSky) {
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

    if (drawAir) {
        // The skybox draws the sky at its intensity, so the air a surface fades into does too.
        const float depth = std::min(ctx.view.camera.zFar, GLAtmosphere::AERIAL_PERSPECTIVE_REACH);
        ctx.atmosphere.setAerialDepth(depth);
        m_aerialPerspective.bind();
        GLAtmosphere::setSky(m_aerialPerspective, sky, sky.sunIlluminance * settings.intensity);
        m_aerialPerspective.setUniform3fv("u_sunDir", sky.sunDir);
        m_aerialPerspective.setUniform1f("u_airDepth", depth);
        m_aerialPerspective.setUniform1f("u_distanceScale", settings.aerialPerspective);
        ctx.atmosphere.bindAerialPerspectiveImage(0, GL_WRITE_ONLY);
        const uint32_t groups = Groups::covering(GLAtmosphere::AERIAL_PERSPECTIVE_SIZE, Groups::IMAGE);
        m_aerialPerspective.dispatch(groups, groups, GLAtmosphere::AERIAL_PERSPECTIVE_SIZE);
        ctx.aerialPerspectiveReady = true;
    }

    // Order the writes before the passes that sample them.
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
}

} // namespace Vkm::Engine
