#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_shadow_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "core/math/frustum.h"
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
    ctx.gl.setDepthFunc(GL_LESS);
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
        renderCasters(ctx, job.lightVP);
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
            renderCasters(ctx, job.faceVP[f]);
        }
    }
}

void GLShadowPass::renderCasters(GLFrameContext& ctx, const glm::mat4& lightVP) {
    const GLView&       glView  = ctx.resources;
    const Math::Frustum frustum = Math::extractFrustum(lightVP);
    const std::vector<ShadowCasterData>& casters = ctx.view.shadowCasters;

    // The caster set is scene-wide; cull it against this tile/face's frustum, then
    // sort the survivors by mesh so identical meshes batch into one instanced draw.
    m_order.clear();
    for (uint32_t i = 0; i < casters.size(); ++i) {
        if (Math::frustumIntersectsAABB(frustum, casters[i].aabbMin, casters[i].aabbMax))
            m_order.push_back(i);
    }
    if (m_order.empty()) return;
    std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
        return casters[a].mesh.id() < casters[b].mesh.id();
    });

    // One instanced depth draw per mesh run. The instance buffer is re-uploaded
    // per run (depth-only, so the handful of re-uploads is cheap); the shadow
    // vertex shader reads the model from the per-instance attribute.
    uint32_t i = 0;
    while (i < m_order.size()) {
        const uint32_t meshId = casters[m_order[i]].mesh.id();
        const GLMesh*  mesh   = glView.getMesh(casters[m_order[i]].mesh);

        m_models.clear();
        uint32_t j = i;
        while (j < m_order.size() && casters[m_order[j]].mesh.id() == meshId) {
            m_models.push_back(casters[m_order[j]].model);
            ++j;
        }

        if (mesh && !m_models.empty()) {
            m_instances.update(m_models.data(), static_cast<uint32_t>(m_models.size()));
            mesh->attachInstances(m_instances, 4);
            mesh->drawInstanced(static_cast<uint32_t>(m_models.size()), 0);
        }
        i = j;
    }
}

} // namespace Engine
