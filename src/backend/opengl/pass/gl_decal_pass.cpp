#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_decal_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "generator/mesh_generators.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
// Flat ambient the decal is lit with on top of the sun, so a decal in shadow
// still reads instead of going black.
constexpr float DECAL_AMBIENT = 0.25f;

glm::vec3 sunRadiance(const RenderView& view) {
    for (const LightData& light : view.lights) {
        if (light.type == LightType::Directional) return light.color * light.intensity;
    }
    return glm::vec3(0.0f);
}
} // namespace

GLDecalPass::GLDecalPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/decal"))
    , m_cube(std::make_unique<GLMesh>(generateCube())) {}

GLDecalPass::~GLDecalPass() = default;

void GLDecalPass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;
    if (view.decals.empty()) return;

    // Blends into the colour chain while sampling the geometry target's depth +
    // G-buffer - a different FBO, so no read-while-write feedback.
    promoteColorChain(ctx);
    ctx.colorSrc->bind(ctx.gl);

    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_FRONT);  // back faces only: one layer, and it survives the camera being inside the box

    m_shader->bind();
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);

    m_shader->setUniformMatrix4fv("u_viewProj",    view.camera.viewProjection);
    m_shader->setUniformMatrix4fv("u_invViewProj", view.camera.invViewProj);
    m_shader->setUniformMatrix4fv("u_invView",     view.camera.invView);
    m_shader->setUniform2f("u_screenSize",
                           static_cast<float>(view.viewportWidth),
                           static_cast<float>(view.viewportHeight));
    m_shader->setUniform3fv("u_sunDir",   ctx.sunDir);
    m_shader->setUniform3fv("u_sunColor", sunRadiance(view));
    m_shader->setUniform1f("u_ambient",   DECAL_AMBIENT * view.environment.sky.intensity);

    for (const DecalData& decal : view.decals) {
        const GLMaterial* material = glView.getMaterial(decal.material);
        if (!material) continue;
        material->bind(GLBindings::UBOBindingPoints::Material);
        material->bindTextures(glView);

        // Decals project along the entity's forward (-Z), matching the engine's
        // orientation convention.
        const glm::vec3 projDir = -glm::normalize(glm::vec3(decal.model[2]));

        m_shader->setUniformMatrix4fv("u_model",    decal.model);
        m_shader->setUniformMatrix4fv("u_invModel", decal.invModel);
        m_shader->setUniform3fv("u_projDir",  projDir);
        m_shader->setUniform1f("u_angleFade", decal.angleFade);
        m_shader->setUniform1f("u_opacity",   decal.opacity);

        m_cube->draw();
    }

    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setDepthTest(true);
}

} // namespace Engine
