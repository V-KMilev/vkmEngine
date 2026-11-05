#include "pass/scene/gl_decal_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "storage/light/gl_shadow_atlas.h"
#include "storage/light/gl_cluster_grid.h"
#include "base/math/rotation.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDecalPass::GLDecalPass()
    : m_shader("shaders/opengl/scene/decal") {}

GLDecalPass::~GLDecalPass() = default;

void GLDecalPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const GLAssetMirror& mirror = ctx.mirror;
    if (view.lists.decals.empty()) return;

    // Blends into the scene's color while sampling the geometry target's depth and G-buffer.
    ctx.colorSrc->bindColorAlone(ctx.gl);

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
    // Every light of a decal's cluster, through its shadow, so a decal agrees with the surface
    // under it: the 2D maps and the cubes.
    ctx.clusters.bind();
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);

    // The environment's light as the surface under the decal takes it, occlusion included.
    bindAmbient(ctx, m_shader);
    bindAO(ctx, m_shader);

    const GLMaterial* bound = nullptr;
    for (const DecalData& decal : view.lists.decals) {
        const GLMaterial* material = mirror.getMaterial(decal.material);
        if (!material) continue;
        // RenderView keeps a material's decals together, so each binds once.
        if (material != bound) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(mirror);
            bound = material;
        }

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
