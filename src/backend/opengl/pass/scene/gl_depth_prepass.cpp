#include "pass/scene/gl_depth_prepass.h"

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "convention/gl_depth.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDepthPrepass::GLDepthPrepass()
    : m_shader("shaders/opengl/scene/forward/prepass")
    , m_skinnedShader("shaders/opengl/scene/forward/prepass", {"SKINNED"})
    , m_waterShader("shaders/opengl/scene/forward/prepass/water") {}

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
    ctx.occlusion->cullAgainst(ctx.sceneRender, ctx.view.sceneWidth(), ctx.view.sceneHeight());
    drawBatch(ctx, GLOcclusionCull::Set::Revealed);
}

void GLDepthPrepass::drawBatch(GLFrameContext& ctx, GLOcclusionCull::Set set) {
    // A double-sided material's back faces lay depth too, or the forward pass's
    // GLDepth::EYE_TEST_EQUAL would find nothing to draw them against.
    const auto doubleSided = [&](const InstanceDraw& draw) {
        const GLMaterial* material = ctx.mirror.getMaterial(draw.material);
        return material && material->doubleSided();
    };
    const Vkm::OpenGL::Shader* boundProgram = nullptr;
    const std::vector<InstanceDraw>& draws  = ctx.opaqueBatch.draws();
    for (size_t i = 0; i < draws.size();) {
        const InstanceDraw& draw     = draws[i];
        Vkm::OpenGL::Shader& program = draw.skinned ? m_skinnedShader : draw.water ? m_waterShader : m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        const bool sides = doubleSided(draw);
        ctx.gl.setFaceCulling(!sides);

        // This pass binds no material, so the draws after this one that need nothing else bound go
        // out with it, their commands following its own; the occlusion cull keeps a list a draw.
        InstanceDraw joined = draw;
        size_t next = i + 1;
        while (!ctx.occlusion && next < draws.size()) {
            const InstanceDraw& after = draws[next];
            const bool alike = after.skinned == draw.skinned && after.water == draw.water
                && after.mirrored == draw.mirrored && after.mesh->layout() == draw.mesh->layout()
                && doubleSided(after) == sides;
            if (!alike) break;
            joined.count += after.count;
            ++next;
        }
        ctx.drawOpaque(joined, set);
        i = next;
    }
    ctx.gl.setFaceCulling(true);
}

} // namespace Vkm::Engine
