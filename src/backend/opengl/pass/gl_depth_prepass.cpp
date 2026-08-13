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
#include "system/render/render_view.h"

namespace Engine {

GLDepthPrepass::GLDepthPrepass()
    : m_shader(std::make_unique<Core::Shader>("shaders/forward/prepass")) {}

GLDepthPrepass::~GLDepthPrepass() = default;

void GLDepthPrepass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // First pass to touch the HDR target: clear all of its attachments (colour,
    // G-buffer, depth) for the frame, then render the opaque G-buffer (view
    // normal + roughness + metalness) into colour attachment 1 while priming
    // depth. The skybox + forward run after and never clear colour, so a
    // transparent surface is never wiped by a later background fill.
    // Re-assert the scene clear colour: offscreen renderers (material previews,
    // probe bakes) set their own backdrop between frames, and the stored colour
    // is applied at clear.
    ctx.gl.setClearColor(glm::vec4(0.0f));
    ctx.sceneRender.clearForFrame(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.sceneRender.bindGBufferPass(ctx.gl);

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_view", view.camera.view);

    // The backend routes only opaque / unlit drawables here (alpha-masked and
    // transparent geometry draws in the forward pass); prime their depth +
    // G-buffer instanced, grouped by (material, mesh). No albedo texture is
    // sampled - the prepass writes normal/roughness/metalness from the UBO - so
    // binding the material UBO is enough.
    const GLMaterial* boundMaterial = nullptr;
    for (const InstanceRun& run : ctx.opaqueBatch.runs()) {
        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::Material);
            boundMaterial = material;
        }
        ctx.opaqueBatch.drawRun(run);
    }
}

} // namespace Engine
