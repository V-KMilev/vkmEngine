#include "pass/gl_depth_prepass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDepthPrepass::GLDepthPrepass()
    : m_shader("shaders/forward/prepass")
    , m_skinnedShader("shaders/forward/prepass_skinned") {}

GLDepthPrepass::~GLDepthPrepass() = default;

void GLDepthPrepass::execute(GLFrameContext& ctx) {
    const GLView& glView = ctx.resources;

    // Set each frame: an offscreen renderer may set its own backdrop between frames.
    // Background G-buffer pixels must stay zero, since their alpha is metalness.
    ctx.gl.setClearColor(glm::vec4(0.0f));
    ctx.sceneRender.clearForFrame(ctx.gl);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.sceneRender.bindGBufferPass(ctx.gl);

    ctx.objects.bind();
    if (ctx.skinPalette.count() > 0) ctx.skinPalette.bind();

    // Material bindings are context state, so this cache survives a program switch.
    const GLMaterial* boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram = nullptr;

    for (const InstanceDraw& draw : ctx.opaqueBatch.draws()) {
        Vkm::GL::Shader& program = draw.skinned ? m_skinnedShader : m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        const GLMaterial* material = glView.getMaterial(draw.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            boundMaterial = material;
        }
        ctx.opaqueBatch.draw(draw);
    }
}

} // namespace Vkm::Engine
