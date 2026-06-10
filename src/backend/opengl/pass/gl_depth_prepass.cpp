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

    // First pass to touch the HDR target: clear all of its attachments (colour,
    // G-buffer, depth) for the frame, then render the opaque G-buffer (view
    // normal + roughness + metalness) into colour attachment 1 while priming
    // depth. The skybox + forward run after and never clear colour, so a
    // transparent surface is never wiped by a later background fill.
    ctx.sceneHDR.clearForFrame(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.sceneHDR.bindGBufferPass(ctx.gl);

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_view", view.camera.view);

    // Collect the non-transparent drawables (transparents draw blended in the
    // forward pass and never prime depth), then draw them instanced, grouped by
    // (material, mesh).
    m_opaque.clear();
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) continue;  // drawn blended later
        m_opaque.push_back(&d);
    }

    const GLMaterial* boundMaterial = nullptr;
    for (const InstanceRun& run : m_batcher.buildGrouped(m_opaque, glView)) {
        const GLMaterial* material = glView.getMaterial(run.material);
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
        m_batcher.drawRun(run);
    }

    ctx.depthPrimed = true;
}

} // namespace Engine
