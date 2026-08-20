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
#include "data/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLShadowPass::GLShadowPass()
    : m_depth2D(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_2d"))
    , m_depthCube(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_cube"))
    , m_depth2DSkinned(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_2d_skinned"))
    , m_depthCubeSkinned(std::make_unique<Vkm::GL::Shader>("shaders/shadow/shadow_cube_skinned")) {}

GLShadowPass::~GLShadowPass() = default;

void GLShadowPass::execute(GLFrameContext& ctx) {
    if (ctx.shadowData.jobs2D().empty() && ctx.shadowData.jobsCube().empty()) return;

    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);

    // The palettes are uploaded before the pass loop, and the skinned programs
    // read them here exactly as the camera ones do downstream.
    ctx.skinPalette.bind();

    render2D(ctx);
    renderCube(ctx);
}

void GLShadowPass::render2D(GLFrameContext& ctx) {
    const std::vector<Shadow2DJob>& jobs = ctx.shadowData.jobs2D();
    if (jobs.empty()) return;

    ctx.shadowAtlas.begin2D(ctx.gl);
    for (size_t j = 0; j < jobs.size(); ++j) {
        ctx.shadowAtlas.setTileViewport(ctx.gl, jobs[j].slot);
        // Uniform state is per program, so the tile's matrix goes to both.
        m_depth2D->bind();
        m_depth2D->setUniformMatrix4fv("u_lightVP", jobs[j].lightVP);
        m_depth2DSkinned->bind();
        m_depth2DSkinned->setUniformMatrix4fv("u_lightVP", jobs[j].lightVP);
        renderCasters(ctx, ctx.shadowData.batch2D(j), *m_depth2D, *m_depth2DSkinned);
    }
}

void GLShadowPass::renderCube(GLFrameContext& ctx) {
    const std::vector<ShadowCubeJob>& jobs = ctx.shadowData.jobsCube();
    for (size_t j = 0; j < jobs.size(); ++j) {
        const ShadowCubeJob& job = jobs[j];
        m_depthCube->bind();
        m_depthCube->setUniform3fv("u_lightPos", job.pos);
        m_depthCube->setUniform1f("u_range", job.range);
        m_depthCubeSkinned->bind();
        m_depthCubeSkinned->setUniform3fv("u_lightPos", job.pos);
        m_depthCubeSkinned->setUniform1f("u_range", job.range);

        for (uint32_t f = 0; f < 6; ++f) {
            ctx.shadowAtlas.beginCubeFace(ctx.gl, job.slot, f);
            m_depthCube->bind();
            m_depthCube->setUniformMatrix4fv("u_faceVP", job.faceVP[f]);
            m_depthCubeSkinned->bind();
            m_depthCubeSkinned->setUniformMatrix4fv("u_faceVP", job.faceVP[f]);
            renderCasters(ctx, ctx.shadowData.batchCube(j, f), *m_depthCube, *m_depthCubeSkinned);
        }
    }
}

void GLShadowPass::renderCasters(GLFrameContext& ctx, const ShadowCasterBatch& batch,
                                 Vkm::GL::Shader& program, Vkm::GL::Shader& skinned) {
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
    program.bind();
    const Vkm::GL::Shader* bound = &program;

    size_t i = 0;
    while (i < order.size()) {
        const uint32_t meshId = casters[order[i]].mesh.id();
        const GLMesh*  mesh   = glView.getMesh(casters[order[i]].mesh);

        const size_t first = i;
        while (i < order.size() && casters[order[i]].mesh.id() == meshId) ++i;
        if (!mesh) continue;

        mesh->attachInstances(m_instances, 4);

        // The common case: one instanced draw for the whole run.
        if (!mesh->isSkinned()) {
            if (bound != &program) { program.bind(); bound = &program; }
            mesh->drawInstanced(static_cast<uint32_t>(i - first), static_cast<uint32_t>(first));
            continue;
        }

        // A skinned run is drawn caster by caster, because the palette base is a
        // uniform here and a uniform describes one draw. A caster the frame did
        // not pose falls back to the static program, which renders the vertices
        // it stored - its bind pose, the same answer the camera path gives it.
        for (size_t k = first; k < i; ++k) {
            const ShadowCasterData& caster = casters[order[k]];
            Vkm::GL::Shader& chosen = (caster.skinCount > 0) ? skinned : program;
            if (bound != &chosen) { chosen.bind(); bound = &chosen; }
            if (caster.skinCount > 0) chosen.setUniform1ui("u_skinBase", caster.skinFirst);
            mesh->drawInstanced(1, static_cast<uint32_t>(k));
        }
    }
}

} // namespace Vkm::Engine
