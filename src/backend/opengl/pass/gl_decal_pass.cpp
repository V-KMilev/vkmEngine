#include "pass/gl_decal_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "storage/gl_shadow_atlas.h"
#include "core/math/rotation.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDecalPass::GLDecalPass()
    : m_shader("shaders/decal") {}

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
    // Back faces only: one layer, and it survives the camera being inside the box.
    ctx.gl.setCullFace(GL_FRONT);

    m_shader.bind();
    bindFog(ctx, m_shader);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, GLBindings::PostTextureSlots::SCENE_GBUFFER);
    // The cascades: compared on one unit, raw on the other for the soft path's
    // blocker search.
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);

    // The key light (see lowestSlotDirectional), so a decal agrees with the surface under it.
    // The cascades are the lowest-slot directional caster's (GLShadowData::build): the key
    // light's exactly when it casts.
    const LightData* key = lowestSlotDirectional(view.lights);
    m_shader.setUniform3fv("u_sunDir",   key ? -key->direction : ctx.sunDir);
    m_shader.setUniform3fv("u_sunColor", key ? key->color * key->intensity : glm::vec3(0.0f));
    m_shader.setUniform1i("u_sunShadowed", (key && key->castShadows) ? 1 : 0);

    // The environment's light as the surface under the decal takes it, occlusion included.
    bindAmbient(ctx, m_shader);
    bindAO(ctx, m_shader);

    for (const DecalData& decal : view.decals) {
        const GLMaterial* material = glView.getMaterial(decal.material);
        if (!material) continue;
        material->bind(GLBindings::UBOBindingPoints::MATERIAL);
        material->bindTextures(glView);

        // A decal projects along its forward; a surface facing back up that ray
        // is the one it lands on.
        const glm::vec3 projDir = Math::computeForward(Math::worldRotationOf(decal.model));

        m_shader.setUniformMatrix4fv("u_model",    decal.model);
        m_shader.setUniformMatrix4fv("u_invModel", decal.invModel);
        m_shader.setUniform3fv("u_projDir",  projDir);
        m_shader.setUniform1f("u_angleFade", decal.angleFade);
        m_shader.setUniform1f("u_opacity",   decal.opacity);

        ctx.unitCube.draw();
    }
}

} // namespace Vkm::Engine
