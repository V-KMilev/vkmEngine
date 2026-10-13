#include "pass/gl_depth_prepass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDepthPrepass::GLDepthPrepass()
    : m_shader("shaders/forward/prepass")
    , m_skinnedShader("shaders/forward/prepass_skinned") {}

GLDepthPrepass::~GLDepthPrepass() = default;

void GLDepthPrepass::execute(GLFrameContext& ctx) {
    // Set each frame: an offscreen renderer may set its own backdrop between frames.
    // Background G-buffer pixels must stay zero, which the reflections read as no surface.
    ctx.gl.setClearColor(glm::vec4(0.0f));
    ctx.sceneRender.clearForFrame(ctx.gl);
    ctx.gl.setDepthFunc(GLDepth::EYE_TEST);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.sceneRender.bindGBufferPass(ctx.gl);

    ctx.objects.bind();
    if (ctx.skinPalette.count() > 0) ctx.skinPalette.bind();

    if (!ctx.occlusion) {
        drawBatch(ctx, GLOcclusionCull::Set::Shown);
        return;
    }

    ctx.occlusion->cullSeen();
    drawBatch(ctx, GLOcclusionCull::Set::Seen);
    ctx.occlusion->cullAgainst(ctx.sceneRender, ctx.view.viewportWidth, ctx.view.viewportHeight);
    drawBatch(ctx, GLOcclusionCull::Set::Revealed);
}

void GLDepthPrepass::drawBatch(GLFrameContext& ctx, GLOcclusionCull::Set set) {
    // A double-sided material's back faces lay depth too, or the forward pass's
    // GLDepth::EYE_TEST_EQUAL would find nothing to draw them against.
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const GLMaterial*      boundMaterial = nullptr;
    for (const InstanceDraw& draw : ctx.opaqueBatch.draws()) {
        Vkm::GL::Shader& program = draw.skinned ? m_skinnedShader : m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        // The material too: a `waves` surface moves here exactly as the forward pass moves it.
        const GLMaterial* material = ctx.resources.getMaterial(draw.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            boundMaterial = material;
        }
        ctx.gl.setFaceCulling(!(material && material->doubleSided()));
        ctx.drawOpaque(draw, set);
    }
    ctx.gl.setFaceCulling(true);
}

} // namespace Vkm::Engine
