#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_cluster_grid.h"
#include "data/gl_irradiance_volume.h"
#include "data/gl_shadow_atlas.h"
#include "gl_view.h"
#include "core/engine_config.h"
#include "convention/gl_bindings.h"
#include "data/gl_material.h"
#include "data/gl_ibl.h"
#include "system/render/render_view.h"

namespace Engine {

GLForwardPass::GLForwardPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/forward/pbr")) {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Camera + light UBOs are already uploaded and bound by the backend.
    ctx.sceneRender.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);

    // The prepass cleared the target and primed opaque depth (it is
    // unconditional in the pass list), and the skybox filled the background
    // before this pass. Match the primed depth with LEQUAL and leave writes off
    // for early-Z. Do NOT clear here - that would wipe the skybox and leave
    // transparents nothing to blend over.
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(false);

    m_shader->bind();

    // The ShadowBlock UBO (binding 3) carries the matrices + slots; here we only
    // bind the depth textures. The light loop samples per light type via each
    // light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::Atlas2D);
    for (uint32_t s = 0; s < Config::MAX_SHADOW_CASTERS_CUBE; ++s) {
        ctx.shadowAtlas.bindCube(s, GLBindings::ShadowTextureSlots::CubeBase + s);
    }

    // u_hasIBL gates the split-sum ambient in the shader; without a baked
    // environment it falls back to flat ambient.
    const bool hasIBL = ctx.ibl.isReady();
    if (hasIBL) {
        ctx.ibl.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
        ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
        ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
    }
    m_shader->setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    m_shader->setUniform1f("u_iblIntensity", view.environment.sky.intensity);

    // The shader multiplies the GTAO factor into the indirect term (ambient/IBL);
    // absent (pass disabled) -> 1.0.
    if (ctx.aoReady) ctx.ao.bindColor(GLBindings::PostTextureSlots::SSAO);
    m_shader->setUniform1i("u_hasSSAO", ctx.aoReady ? 1 : 0);

    // View -> world, so the GTAO bent normal can be used in world space.
    m_shader->setUniformMatrix4fv("u_invView", view.camera.invView);

    // The shader needs the baked SH volume's box to place a fragment in the grid.
    const bool hasIV = ctx.irradiance.isReady() && !view.irradianceVolumes.empty();
    m_shader->setUniform1i("u_hasIrradianceVolume", hasIV ? 1 : 0);
    if (hasIV) {
        const IrradianceVolumeData& iv = view.irradianceVolumes[0];
        ctx.irradiance.bindSlot(0, GLBindings::IrradianceVolumeSlots::SH0);
        ctx.irradiance.bindSlot(1, GLBindings::IrradianceVolumeSlots::SH1);
        ctx.irradiance.bindSlot(2, GLBindings::IrradianceVolumeSlots::SH2);
        ctx.irradiance.bindSlot(3, GLBindings::IrradianceVolumeSlots::SH3);
        m_shader->setUniform3fv("u_ivMin",  iv.center - iv.halfExtents);
        m_shader->setUniform3fv("u_ivSize", iv.halfExtents * 2.0f);
        m_shader->setUniform1f("u_ivIntensity", iv.intensity);
    }

    // The backend bound the probe cube arrays + ProbeBlock UBO; this only hands
    // over the active count, or 0 when probes are toggled off.
    m_shader->setUniform1i("u_probeCount", ctx.view.settings.probes ? ctx.probeCount : 0);

    // Refraction is sampled only by the transparent bucket; switched on after the
    // scene-colour copy below.
    m_shader->setUniform1i("u_hasSceneColor", 0);
    m_shader->setUniform2f("u_screenSize", static_cast<float>(view.viewportWidth), static_cast<float>(view.viewportHeight));

    // The grid the cull compute wrote; near/far go with it in the same
    // two-coefficient form as the cull pass, for the fragment's cluster lookup.
    ctx.clusters.bind();
    m_shader->setUniform1i("u_useClusters", 1);
    m_shader->setUniform1i("u_renderMode", static_cast<int>(view.settings.renderMode));
    m_shader->setUniform1f("u_zNear", view.camera.zNear);
    m_shader->setUniform1f("u_zFar",  view.camera.zFar);

    // Sorted upstream by material+mesh and batched once by the backend; the
    // prepass drew these same runs.
    drawRuns(ctx, ctx.opaqueBatch);

    if (!ctx.alphaMask.empty()) {
        // Alpha-masked geometry (foliage / fences / grates) is not in the
        // prepass, so it primes its own depth here: writes on, LEQUAL over the
        // opaque scene. Under MSAA, GL_SAMPLE_ALPHA_TO_COVERAGE turns the
        // shader's sharpened cutout alpha into anti-aliased edges; at 1 sample
        // it is a no-op (a hard cutout). Enabled raw - the Context deliberately
        // does not model this one-off state, and the enable/disable is paired.
        ctx.gl.setDepthWrite(true);
        ctx.gl.setDepthFunc(GL_LEQUAL);
        const bool a2c = ctx.view.settings.msaaSamples > 1;
        if (a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        m_batcher.buildGrouped(ctx.alphaMask, glView);
        drawRuns(ctx, GLInstanceBatchView(m_batcher));
        if (a2c) glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        ctx.gl.setDepthWrite(false);  // back to the early-Z state for the transparent grab
    }

    if (!ctx.transparent.empty()) {
        // Back-to-front, so alpha blending composes correctly.
        m_transparent.clear();
        m_transparent.reserve(ctx.transparent.size());
        for (const DrawableData* d : ctx.transparent) {
            const glm::vec3 toCam = view.camera.position - glm::vec3(d->model[3]);
            m_transparent.emplace_back(glm::dot(toCam, toCam), d);
        }
        std::sort(m_transparent.begin(), m_transparent.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        m_transparentSorted.clear();
        m_transparentSorted.reserve(m_transparent.size());
        for (const auto& entry : m_transparent) m_transparentSorted.push_back(entry.second);

        // Copy the opaque + sky scene so transmissive surfaces can refract what
        // is behind them; blitColorFrom resolves the multisample colour into the
        // single-sample scratch.
        ctx.colorDst->blitColorFrom(ctx.sceneRender);
        ctx.sceneRender.bind(ctx.gl);
        ctx.colorDst->bindColor(GLBindings::PostTextureSlots::SceneColor);
        m_shader->setUniform1i("u_hasSceneColor", 1);

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        m_batcher.buildSequential(m_transparentSorted, glView);
        drawRuns(ctx, GLInstanceBatchView(m_batcher));

        ctx.gl.setBlending(false);
        ctx.gl.setDepthWrite(true);
    }

    // Leave the engine-default depth state so the next pass never inherits our
    // early-Z setup. depthWrite especially: the early-Z path turns it off and the
    // transparent block only restores it when transparents were actually drawn,
    // so without this the exit state would depend on this frame's content.
    ctx.gl.setFaceCulling(false);
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(true);
}

void GLForwardPass::drawRuns(GLFrameContext& ctx, const GLInstanceBatchView& batch) {
    batch.bindInstanceData();

    const std::vector<InstanceRun>& runs = batch.runs();
    const GLView& glView = ctx.resources;

    const GLMaterial* boundMaterial = nullptr;

    for (uint32_t i = 0; i < runs.size(); ++i) {
        const InstanceRun& run = runs[i];
        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::Material);
            material->bindTextures(glView);
            boundMaterial = material;
        }
        batch.draw(run, i);
    }
}

} // namespace Engine
