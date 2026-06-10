#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_shadow_pass.h"

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "data/gl_mesh.h"
#include "data/gl_shadow_atlas.h"
#include "data/gl_shadow_data.h"
#include "system/render/render_view.h"

namespace Engine {

GLShadowPass::GLShadowPass()
    : m_depth2D(std::make_unique<Core::Shader>("shaders/shadow/shadow_2d"))
    , m_depthCube(std::make_unique<Core::Shader>("shaders/shadow/shadow_cube")) {}

GLShadowPass::~GLShadowPass() = default;

void GLShadowPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;
    if (ctx.shadowData.jobs2D().empty() && ctx.shadowData.jobsCube().empty()) return;

    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);

    render2D(ctx);
    renderCube(ctx);
}

void GLShadowPass::render2D(GLFrameContext& ctx) {
    // Directional cascades + spot maps. One clear, then one tile per job.
    const std::vector<Shadow2DJob>& jobs = ctx.shadowData.jobs2D();
    if (jobs.empty()) return;

    ctx.shadowAtlas.begin2D(ctx.gl);
    m_depth2D->bind();
    for (const Shadow2DJob& job : jobs) {
        ctx.shadowAtlas.setTileViewport(ctx.gl, job.slot);
        m_depth2D->setUniformMatrix4fv("u_lightVP", job.lightVP);
        renderCasters(ctx, *m_depth2D);
    }
}

void GLShadowPass::renderCube(GLFrameContext& ctx) {
    // Point lights, six faces each (linear distance depth).
    for (const ShadowCubeJob& job : ctx.shadowData.jobsCube()) {
        m_depthCube->bind();
        m_depthCube->setUniform3fv("u_lightPos", job.pos);
        m_depthCube->setUniform1f("u_range", job.range);
        for (uint32_t f = 0; f < 6; ++f) {
            ctx.shadowAtlas.beginCubeFace(ctx.gl, job.slot, f);
            m_depthCube->setUniformMatrix4fv("u_faceVP", job.faceVP[f]);
            renderCasters(ctx, *m_depthCube);
        }
    }
}

void GLShadowPass::renderCasters(GLFrameContext& ctx, Core::Shader& shader) {
    const GLView& glView = ctx.resources;
    for (const DrawableData& d : ctx.view.drawables) {
        if (!d.castShadows) continue;
        const GLMesh* mesh = glView.getMesh(d.mesh);
        if (!mesh) continue;
        shader.setUniformMatrix4fv("u_model", d.model);
        mesh->draw();
    }
}

} // namespace Engine
