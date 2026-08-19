#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_shadow_pass.h"

#include <GL/glew.h>

#include "debug/profiler.h"

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
    : m_depth2D(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_2d"))
    , m_depthCube(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_cube")) {}

GLShadowPass::~GLShadowPass() = default;

void GLShadowPass::execute(GLFrameContext& ctx) {
    if (ctx.shadowData.jobs2D().empty() && ctx.shadowData.jobsCube().empty()) return;

    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);

    render2D(ctx);
    renderCube(ctx);
}

void GLShadowPass::render2D(GLFrameContext& ctx) {
    const std::vector<Shadow2DJob>& jobs = ctx.shadowData.jobs2D();
    if (jobs.empty()) return;

    ctx.shadowAtlas.begin2D(ctx.gl);
    m_depth2D->bind();
    for (size_t j = 0; j < jobs.size(); ++j) {
        ctx.shadowAtlas.setTileViewport(ctx.gl, jobs[j].slot);
        m_depth2D->setUniformMatrix4fv("u_lightVP", jobs[j].lightVP);
        renderCasters(ctx, ctx.shadowData.batch2D(j));
    }
}

void GLShadowPass::renderCube(GLFrameContext& ctx) {
    m_depthCube->bind();
    const std::vector<ShadowCubeJob>& jobs = ctx.shadowData.jobsCube();
    for (size_t j = 0; j < jobs.size(); ++j) {
        const ShadowCubeJob& job = jobs[j];
        m_depthCube->setUniform3fv("u_lightPos", job.pos);
        m_depthCube->setUniform1f("u_range", job.range);
        for (uint32_t f = 0; f < 6; ++f) {
            ctx.shadowAtlas.beginCubeFace(ctx.gl, job.slot, f);
            m_depthCube->setUniformMatrix4fv("u_faceVP", job.faceVP[f]);
            renderCasters(ctx, ctx.shadowData.batchCube(j, f));
        }
    }
}

void GLShadowPass::renderCasters(GLFrameContext& ctx, const ShadowCasterBatch& batch) {
    if (batch.order.empty()) return;

    const GLView& glView = ctx.resources;
    const std::vector<ShadowCasterData>& casters = ctx.view.shadowCasters;
    const std::vector<uint32_t>&         order   = batch.order;

    // Culling and mesh-sorting already happened on the thread pool (see
    // GLShadowData::cullCasters), so this is submission only: one upload, then
    // one draw per mesh run from its slice via baseInstance. Depth-only, so the
    // instance data is just the model matrix - no normal matrix.
    {
    PROFILE_SCOPE("ShadowCasters/Gather");
    m_models.clear();
    m_models.reserve(order.size());
    for (uint32_t idx : order) m_models.push_back(casters[idx].model);
    }
    {
    PROFILE_SCOPE("ShadowCasters/Upload");
    m_instances.update(m_models.data(), static_cast<uint32_t>(m_models.size()));
    }

    PROFILE_SCOPE("ShadowCasters/Draw");
    size_t i = 0;
    while (i < order.size()) {
        const uint32_t meshId = casters[order[i]].mesh.id();
        const GLMesh*  mesh   = glView.getMesh(casters[order[i]].mesh);

        const size_t first = i;
        while (i < order.size() && casters[order[i]].mesh.id() == meshId) ++i;
        const uint32_t count = static_cast<uint32_t>(i - first);

        if (mesh) {
            mesh->attachInstances(m_instances, 4);
            mesh->drawInstanced(count, static_cast<uint32_t>(first));
        }
    }
}

} // namespace Engine
