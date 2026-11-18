#include "pass/scene/gl_outline_pass.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "asset/gl_mesh.h"
#include "convention/gl_depth.h"
#include "frame/draw/gl_instance_batcher.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLOutlinePass::GLOutlinePass()
    : m_shader("shaders/opengl/scene/outline")
    , m_skinnedShader("shaders/opengl/scene/outline", {"SKINNED"}) {}

GLOutlinePass::~GLOutlinePass() = default;

void GLOutlinePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.lists.outlines.empty()) return;

    // A command an outlined mesh, so each is drawn in its own width and color.
    const RenderObjects& objects = *view.objects;
    const bool posed = ctx.skinPalette.count() > 0;
    m_rims.clear();
    m_list.clear();
    for (uint32_t o = 0; o < view.lists.outlines.size(); ++o) {
        const uint32_t object  = view.lists.outlines[o].object;
        const ObjectDraw& draw = objects.draws[object];
        const GLMesh* mesh     = ctx.mirror.getMesh(draw.mesh);
        if (!mesh) continue;
        const bool mirrored = glm::determinant(glm::mat3(objects.models[object])) < 0.0f;
        m_rims.push_back({mesh, o, posed && draw.skinCount > 0 && mesh->isSkinned(), mirrored});
        m_list.commands().push_back(mesh->command(1, static_cast<uint32_t>(m_list.instances().size())));
        m_list.instances().push_back(object);
    }
    if (m_rims.empty()) return;
    m_list.upload(ctx.mirror.stream());

    ctx.objects.bind();
    if (posed) ctx.skinPalette.bind();

    // Where nothing stands in front, and with the reflection inputs zeroed under it, so what a
    // mirror behind it reflects is not added onto it.
    ctx.sceneRender.bindForwardPass(ctx.gl);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setDepthFunc(GLDepth::EYE_TEST);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_FRONT);
    drawMeshes(ctx, Which::All, 1.0f, 1.0f, 1.0f);

    const auto throughWalls = [](const OutlineData& outline) { return outline.throughWalls; };
    if (std::none_of(view.lists.outlines.begin(), view.lists.outlines.end(), throughWalls)) return;

    // Through walls, untested: the color's alpha is 1 under the rim's mesh and 0 under the mesh
    // itself, and the rim lands where it is 1. Nothing after this pass reads that alpha.
    ctx.sceneRender.bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setColorMask(false, false, false, true);
    drawMeshes(ctx, Which::ThroughWalls, 1.0f, 0.0f, 1.0f);
    drawMeshes(ctx, Which::ThroughWalls, 0.0f, 0.0f, 0.0f);
    ctx.gl.setColorMask(true, true, true, false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
    drawMeshes(ctx, Which::ThroughWalls, 1.0f, 1.0f, 1.0f);
}

void GLOutlinePass::drawMeshes(GLFrameContext& ctx, Which which, float widen, float tint, float alpha) {
    const RenderView& view = ctx.view;
    // A pixel of the window in clip units at w = 1, whatever the scene's render scale.
    const glm::vec2 pixel(
        2.0f / static_cast<float>(std::max(view.viewportWidth, 1u)),
        2.0f / static_cast<float>(std::max(view.viewportHeight, 1u))
    );
    // A value the exposure brings back to itself, as the outline's color is shown.
    const float unexposed = std::exp2(-ctx.exposure);

    const Vkm::OpenGL::Shader* bound = nullptr;
    for (uint32_t r = 0; r < m_rims.size(); ++r) {
        const Rim& rim = m_rims[r];
        const OutlineData& outline = view.lists.outlines[rim.outline];
        if (which == Which::ThroughWalls && !outline.throughWalls) continue;

        Vkm::OpenGL::Shader& program = rim.skinned ? m_skinnedShader : m_shader;
        if (&program != bound) {
            program.bind();
            bound = &program;
        }
        program.setUniform2f("u_pixel", pixel.x, pixel.y);
        program.setUniform1f("u_width", outline.width * widen);
        program.setUniform4fv("u_color", glm::vec4(outline.color * (tint * unexposed), alpha));

        const GLenum front = turnForMirror(ctx.gl, rim.mirrored);
        m_list.draw(*rim.mesh, r, 1);
        ctx.gl.setFrontFace(front);
    }
}

} // namespace Vkm::Engine
