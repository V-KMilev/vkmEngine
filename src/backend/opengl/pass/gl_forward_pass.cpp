#include "pass/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "storage/gl_cluster_grid.h"
#include "storage/gl_irradiance_volume.h"
#include "storage/gl_shadow_atlas.h"
#include "gl_view.h"
#include "core/engine_config.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "storage/gl_ibl.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLForwardPass::GLForwardPass()
    : m_shader("shaders/forward/pbr")
    , m_skinnedShader("shaders/forward/pbr_skinned") {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Camera and lights are bound by the backend. Every bucket writes the reflection inputs
    // while the target carries them; the transparent one only dims them (see below).
    ctx.sceneRender.bindForwardPass(ctx.gl);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.gl.setDepthWrite(false);

    // ShadowBlock carries matrices and slots; only the depth textures bind here. The light loop
    // picks a slot from each light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    // The soft path searches the tile for blockers, which is a depth read rather
    // than a compare: the same atlas, on a second unit under a plain sampler.
    ctx.shadowAtlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
    for (uint32_t s = 0; s < Config::MAX_SHADOW_CASTERS_CUBE; ++s) {
        ctx.shadowAtlas.bindCube(s, GLBindings::ShadowTextureSlots::CUBE_BASE + s);
    }

    // The irradiance cube and the volume beside these are bindAmbient's. The
    // LUT is integrated at start, so it is there with or without a sky.
    if (ctx.ibl.isReady()) ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);

    ctx.clusters.bind();
    ctx.objects.bind();

    // With no palette no run is skinned, so the skinned program is never bound
    // and never told anything.
    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();

    bindFrameUniforms(m_shader, ctx);
    if (posed) bindFrameUniforms(m_skinnedShader, ctx);

    bindBucketUniforms(m_shader, ctx, Bucket::Opaque);
    if (posed) bindBucketUniforms(m_skinnedShader, ctx, Bucket::Opaque);

    // Batched once per frame (see GLFrameContext::opaqueBatch).
    drawBatch(ctx, ctx.opaqueBatch);

    if (!ctx.alphaMask.empty()) {
        // GL_SAMPLE_ALPHA_TO_COVERAGE turns the shader's sharpened cutout alpha into
        // anti-aliased edges under MSAA; at 1 sample it is a hard cutout. Enabled raw:
        // the Context does not model this one-off state, and the pair is closed below.
        ctx.gl.setDepthWrite(true);
        const bool a2c = ctx.sceneRender.samples() > 1;
        if (a2c) VKM_GL_CHECK(glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE));
        bindBucketUniforms(m_shader, ctx, Bucket::AlphaMask);
        if (posed) bindBucketUniforms(m_skinnedShader, ctx, Bucket::AlphaMask);
        m_batcher.buildGrouped(ctx.alphaMask, *view.objects, glView, ctx.skinPalette.count());
        drawBatch(ctx, m_batcher);
        if (a2c) VKM_GL_CHECK(glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE));
    }

    if (view.settings.renderMode == RenderMode::Wireframe) drawWireframe(ctx, posed);

    if (!ctx.transparent.empty()) {
        // Back-to-front, so alpha blending composes correctly.
        const std::vector<glm::mat4>& models = view.objects->models;
        m_transparent.clear();
        m_transparent.reserve(ctx.transparent.size());
        for (const uint32_t object : ctx.transparent) {
            const glm::vec3 toCam = view.camera.position - glm::vec3(models[object][3]);
            m_transparent.emplace_back(glm::dot(toCam, toCam), object);
        }
        std::sort(
            m_transparent.begin(),
            m_transparent.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; }
        );

        m_transparentSorted.clear();
        m_transparentSorted.reserve(m_transparent.size());
        for (const auto& entry : m_transparent) m_transparentSorted.push_back(entry.second);

        // Copy the opaque + sky scene for transmissive surfaces to refract; blitColorFrom
        // resolves the multisample colour into the single-sample scratch.
        ctx.colorDst->blitColorFrom(ctx.sceneRender);
        ctx.sceneRender.bindForwardPass(ctx.gl);
        ctx.colorDst->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
        bindBucketUniforms(m_shader, ctx, Bucket::Transparent);
        if (posed) bindBucketUniforms(m_skinnedShader, ctx, Bucket::Transparent);

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        // A transparent surface does not reflect through the reflection pass;
        // it dims what the surface behind it reflects, by its own opacity.
        setReflectRoughnessWritable(false);

        m_batcher.buildSequential(m_transparentSorted, *view.objects, glView, ctx.skinPalette.count());
        drawBatch(ctx, m_batcher);

        setReflectRoughnessWritable(true);
    }
}

void GLForwardPass::drawBatch(GLFrameContext& ctx, const GLInstanceBatcher& batch) {
    const GLView& glView = ctx.resources;

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;

    for (const InstanceDraw& draw : batch.draws()) {
        // Grouped batches sort skinned runs together, so this switches once; a
        // sequential (transparent) batch may alternate, to keep its depth order.
        Vkm::GL::Shader& program = draw.skinned ? m_skinnedShader : m_shader;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        const GLMaterial* material = glView.getMaterial(draw.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
        }
        batch.draw(draw);
    }
}

void GLForwardPass::drawWireframe(GLFrameContext& ctx, bool posed) {
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Raw, as alpha-to-coverage is: the Context does not model these, and they are closed below.
    VKM_GL_CHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
    VKM_GL_CHECK(glEnable(GL_POLYGON_OFFSET_LINE));
    VKM_GL_CHECK(glPolygonOffset(-1.0f, -1.0f));

    m_shader.bind();
    m_shader.setUniform1i("u_wirePass", 1);
    if (posed) {
        m_skinnedShader.bind();
        m_skinnedShader.setUniform1i("u_wirePass", 1);
    }

    drawBatch(ctx, ctx.opaqueBatch);
    // Still the alpha-masked batch: the transparent one is built after this.
    if (!ctx.alphaMask.empty()) drawBatch(ctx, m_batcher);

    m_shader.bind();
    m_shader.setUniform1i("u_wirePass", 0);
    if (posed) {
        m_skinnedShader.bind();
        m_skinnedShader.setUniform1i("u_wirePass", 0);
    }

    VKM_GL_CHECK(glDisable(GL_POLYGON_OFFSET_LINE));
    VKM_GL_CHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
    ctx.gl.setBlending(false);
}

void GLForwardPass::bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx) const {
    const RenderView& view = ctx.view;

    shader.bind();

    // u_hasIBL gates the split-sum ambient in the shader; without a baked
    // environment it falls back to flat ambient.
    bindAmbient(ctx, shader);

    // The backend bound the probe arrays and ProbeBlock; this hands over the count, 0 when off.
    shader.setUniform1i("u_probeCount", view.settings.probes ? ctx.probeCount : 0);

    // Every bucket fogs itself at its own depth, so the opaque scene the
    // refraction copies and the transparents blend over is fogged already.
    bindFog(ctx, shader);

    shader.setUniform1i("u_useClusters", 1);
    shader.setUniform1i("u_renderMode", static_cast<int>(view.settings.renderMode));
}

void GLForwardPass::bindBucketUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx, Bucket bucket) const {
    shader.bind();
    // GTAO saw only what the prepass drew.
    bindAO(ctx, shader, bucket == Bucket::Opaque);
    shader.setUniform1i("u_hasSceneColor", bucket == Bucket::Transparent ? 1 : 0);
    shader.setUniform1i(
        "u_alphaToCoverage",
        (bucket == Bucket::AlphaMask && ctx.sceneRender.samples() > 1) ? 1 : 0
    );
}

} // namespace Vkm::Engine
