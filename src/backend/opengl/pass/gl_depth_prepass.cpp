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
#include "data/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDepthPrepass::GLDepthPrepass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/forward/prepass"))
    , m_skinnedShader(std::make_unique<Vkm::GL::Shader>("shaders/forward/prepass_skinned")) {}

GLDepthPrepass::~GLDepthPrepass() = default;

void GLDepthPrepass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // First pass to touch the HDR target, so it clears all of the attachments
    // (colour, G-buffer, depth) for the frame. The skybox + forward run after
    // and never clear colour, so a transparent surface is never wiped by a later
    // background fill.
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

    // Uniform state is per program in GL, so the view matrix has to be set on
    // each of them, not once on whichever happens to be bound.
    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_view", view.camera.view);
    m_skinnedShader->bind();
    m_skinnedShader->setUniformMatrix4fv("u_view", view.camera.view);

    ctx.skinPalette.bind();

    // No albedo texture is sampled - the prepass writes normal/roughness/
    // metalness from the UBO - so binding the material UBO is enough. The
    // material bindings are context state, not program state, so the cache
    // below survives a program switch.
    const GLMaterial* boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram = nullptr;
    ctx.opaqueBatch.bindInstanceData();

    const std::vector<InstanceRun>& runs = ctx.opaqueBatch.runs();
    for (uint32_t i = 0; i < runs.size(); ++i) {
        const InstanceRun& run = runs[i];

        Vkm::GL::Shader& program = run.skinned ? *m_skinnedShader : *m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::Material);
            boundMaterial = material;
        }
        ctx.opaqueBatch.draw(run, i);
    }
}

} // namespace Vkm::Engine
