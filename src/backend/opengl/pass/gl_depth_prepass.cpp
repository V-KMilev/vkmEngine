#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_depth_prepass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "system/render/render_view.h"

namespace Engine {

GLDepthPrePass::GLDepthPrePass()
    : m_shader(std::make_unique<Core::Shader>("shaders/forward/prepass")) {}

GLDepthPrePass::~GLDepthPrePass() = default;

void GLDepthPrePass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Fill the scene target's depth with all opaque geometry. This is the first
    // pass to bind the HDR target, so it also clears colour for the frame: the
    // skybox and forward run after and never clear, so a transparent surface is
    // never wiped by a later background fill.
    ctx.sceneHDR.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.gl.clear(true, true, false);

    m_shader->bind();

    const GLMaterial* boundMaterial = nullptr;
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) continue;  // drawn blended later

        const GLMesh* mesh = glView.getMesh(d.mesh);
        if (!mesh) continue;

        if (material && material != boundMaterial) {
            // The UBO carries type/cutoff for the alpha-mask branch; only
            // alpha-masked materials sample albedo, so skip texture binds
            // (and their cost) for the common opaque case.
            material->bind(GLBindings::UBOBindingPoints::Material);
            if (material->getType() == MaterialType::AlphaMask) {
                material->bindTextures(glView);
            }
            boundMaterial = material;
        }

        m_shader->setUniformMatrix4fv("u_model", d.model);
        mesh->draw();
    }

    ctx.depthPrimed = true;
}

} // namespace Engine
