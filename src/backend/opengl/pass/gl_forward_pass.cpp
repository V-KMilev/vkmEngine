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
#include "data/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLForwardPass::GLForwardPass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/forward/pbr"))
    , m_skinnedShader(std::make_unique<Vkm::GL::Shader>("shaders/forward/pbr_skinned")) {}

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
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(false);

    // The ShadowBlock UBO (binding 3) carries the matrices and slots; only the depth
    // textures are bound here. The light loop picks a slot per light type from that
    // light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    for (uint32_t s = 0; s < Config::MAX_SHADOW_CASTERS_CUBE; ++s) {
        ctx.shadowAtlas.bindCube(s, GLBindings::ShadowTextureSlots::CUBE_BASE + s);
    }

    if (ctx.ibl.isReady()) {
        ctx.ibl.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
        ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
        ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);
    }

    // The shader multiplies the GTAO factor into the indirect term (ambient/IBL);
    // absent (pass disabled) -> 1.0.
    if (ctx.aoReady) ctx.ao.bindColor(GLBindings::PostTextureSlots::SSAO);

    if (ctx.irradiance.isReady() && !view.irradianceVolumes.empty()) {
        ctx.irradiance.bindSlot(0, GLBindings::IrradianceVolumeSlots::SH0);
        ctx.irradiance.bindSlot(1, GLBindings::IrradianceVolumeSlots::SH1);
        ctx.irradiance.bindSlot(2, GLBindings::IrradianceVolumeSlots::SH2);
        ctx.irradiance.bindSlot(3, GLBindings::IrradianceVolumeSlots::SH3);
    }

    // The grid the cull compute wrote, and - on a frame that posed something -
    // the bone palettes its skinned runs read. With no palette no run is
    // skinned, so the skinned program is never bound and never told anything.
    ctx.clusters.bind();

    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();

    bindFrameUniforms(*m_shader, ctx, false);
    if (posed) bindFrameUniforms(*m_skinnedShader, ctx, false);

    // Sorted upstream by material+mesh and batched once by the backend; the
    // prepass drew these same runs.
    drawRuns(ctx, ctx.opaqueBatch);

    if (!ctx.alphaMask.empty()) {
        // GL_SAMPLE_ALPHA_TO_COVERAGE turns the shader's sharpened cutout alpha into
        // anti-aliased edges under MSAA; at 1 sample it is a hard cutout. Enabled raw:
        // the Context does not model this one-off state, and the pair is closed below.
        ctx.gl.setDepthWrite(true);
        ctx.gl.setDepthFunc(GL_LEQUAL);
        const bool a2c = ctx.view.settings.msaaSamples > 1;
        if (a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        m_batcher.buildGrouped(ctx.alphaMask, glView, ctx.skinPalette.count());
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
        ctx.colorDst->bindColor(GLBindings::PostTextureSlots::SCENE_COLOR);
        bindFrameUniforms(*m_shader, ctx, true);
        if (posed) bindFrameUniforms(*m_skinnedShader, ctx, true);

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        m_batcher.buildSequential(m_transparentSorted, glView, ctx.skinPalette.count());
        drawRuns(ctx, GLInstanceBatchView(m_batcher));

        ctx.gl.setBlending(false);
        ctx.gl.setDepthWrite(true);
    }

    // Leave the engine-default depth state: the early-Z path turns depthWrite off and
    // the transparent block restores it only when transparents drew, so without this the
    // state the next pass inherits would depend on this frame's content.
    ctx.gl.setFaceCulling(false);
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(true);
}

void GLForwardPass::drawRuns(GLFrameContext& ctx, const GLInstanceBatchView& batch) {
    batch.bindInstanceData();

    const std::vector<InstanceRun>& runs = batch.runs();
    const GLView& glView = ctx.resources;

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;

    for (uint32_t i = 0; i < runs.size(); ++i) {
        const InstanceRun& run = runs[i];

        // Grouped batches sort skinned runs together, so this switches once; a
        // sequential (transparent) batch may alternate, which is correct and no
        // worse than the depth order it is preserving.
        Vkm::GL::Shader& program = run.skinned ? *m_skinnedShader : *m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
        }
        batch.draw(run, i);
    }
}

void GLForwardPass::bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx,
                                      bool hasSceneColor) const {
    const RenderView& view = ctx.view;

    shader.bind();

    // u_hasIBL gates the split-sum ambient in the shader; without a baked
    // environment it falls back to flat ambient.
    const bool hasIBL = ctx.ibl.isReady();
    shader.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    shader.setUniform1f("u_iblIntensity", view.environment.sky.intensity);

    shader.setUniform1i("u_hasSSAO", ctx.aoReady ? 1 : 0);

    // View -> world, so the GTAO bent normal can be used in world space.
    shader.setUniformMatrix4fv("u_invView", view.camera.invView);

    // The shader needs the baked SH volume's box to place a fragment in the grid.
    const bool hasIV = ctx.irradiance.isReady() && !view.irradianceVolumes.empty();
    shader.setUniform1i("u_hasIrradianceVolume", hasIV ? 1 : 0);
    if (hasIV) {
        const IrradianceVolumeData& iv = view.irradianceVolumes[0];
        shader.setUniform3fv("u_ivMin",  iv.center - iv.halfExtents);
        shader.setUniform3fv("u_ivSize", iv.halfExtents * 2.0f);
        shader.setUniform1f("u_ivIntensity", iv.intensity);
    }

    // The backend bound the probe cube arrays + ProbeBlock UBO; this only hands
    // over the active count, or 0 when probes are toggled off.
    shader.setUniform1i("u_probeCount", view.settings.probes ? ctx.probeCount : 0);

    // Refraction is sampled only by the transparent bucket, after the
    // scene-colour copy.
    shader.setUniform1i("u_hasSceneColor", hasSceneColor ? 1 : 0);
    shader.setUniform2f("u_screenSize", static_cast<float>(view.viewportWidth),
                                        static_cast<float>(view.viewportHeight));

    // near/far go with the cluster grid in the same two-coefficient form as the
    // cull pass, for the fragment's cluster lookup.
    shader.setUniform1i("u_useClusters", 1);
    shader.setUniform1i("u_renderMode", static_cast<int>(view.settings.renderMode));
    shader.setUniform1f("u_zNear", view.camera.zNear);
    shader.setUniform1f("u_zFar",  view.camera.zFar);
}

} // namespace Vkm::Engine
