#include "pass/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "storage/gl_cluster_grid.h"
#include "storage/gl_irradiance_volume.h"
#include "storage/gl_lightmaps.h"
#include "storage/gl_shadow_atlas.h"
#include "storage/gl_ocean_fields.h"
#include "storage/gl_ocean_shore.h"
#include "gl_view.h"
#include "core/engine_config.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "storage/gl_ibl.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLForwardPass::GLForwardPass(GLForwardPrograms& programs)
    : m_programs(programs) {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Camera and lights are bound by the backend. Every bucket writes the reflection inputs
    // while a reflection reads them (GLPass::reflecting); the transparent one only dims them.
    ctx.sceneRender.bindForwardPass(ctx.gl, reflecting(ctx));
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.gl.setDepthWrite(false);

    // ShadowBlock carries matrices and slots; only the depth textures bind here. The light loop
    // picks a slot from each light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    // The soft path searches the tile for blockers, which is a depth read rather
    // than a compare: the same atlas, on a second unit under a plain sampler.
    ctx.shadowAtlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
    // The point lights' cubes likewise: compared, and raw for their blocker search.
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);
    ctx.shadowAtlas.bindCubesRaw(GLBindings::ShadowTextureSlots::CUBE_RAW);

    // The irradiance cube and the volume beside these are bindAmbient's. The
    // LUT is integrated at start, so it is there with or without a sky.
    if (ctx.ibl.isReady()) ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);

    ctx.clusters.bind();
    ctx.objects.bind();
    // The ocean's ripples and shore, if the frame made them; with none its water reads neither.
    // The static programs alone: a skinned mesh is never moved as water, so has no waterline.
    ctx.oceanFields.bind();
    ctx.oceanShore.bind();
    if (ctx.lightmaps) ctx.lightmaps->bindPages();
    forEachProgram(false, [&](Vkm::GL::Shader& program) {
        program.setUniform4fv("u_oceanShore", ctx.oceanShore.area());
    });

    // With no palette no run is skinned, so the skinned programs are never bound
    // and never told anything.
    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();

    forEachProgram(posed, [&](Vkm::GL::Shader& program) {
        bindFrameUniforms(program, ctx);
        bindBucketUniforms(program, ctx, Bucket::Opaque);
    });

    // Batched once per frame (see GLFrameContext::opaqueBatch).
    drawBatch(ctx, ctx.opaqueBatch);

    if (!ctx.alphaMask.empty()) {
        // Alpha to coverage turns the shader's sharpened cutout alpha into anti-aliased edges
        // under MSAA; at 1 sample it is a hard cutout.
        ctx.gl.setDepthWrite(true);
        ctx.gl.setAlphaToCoverage(ctx.sceneRender.samples() > 1);
        forEachProgram(posed, [&](Vkm::GL::Shader& program) {
            bindBucketUniforms(program, ctx, Bucket::AlphaMask);
        });
        m_batcher.buildGrouped(ctx.alphaMask, *view.objects, glView, ctx.skinPalette.count());
        drawBatch(ctx, m_batcher);
        ctx.gl.setAlphaToCoverage(false);
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
        // resolves the multisample color into the single-sample scratch.
        ctx.colorDst->blitColorFrom(ctx.sceneRender);
        ctx.sceneRender.bindForwardPass(ctx.gl, reflecting(ctx));
        ctx.colorDst->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
        forEachProgram(posed, [&](Vkm::GL::Shader& program) {
            bindBucketUniforms(program, ctx, Bucket::Transparent);
        });

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        // A transparent surface does not reflect through the reflection pass;
        // it dims what the surface behind it reflects, by its own opacity.
        setReflectRoughnessWritable(ctx.gl, false);

        m_batcher.buildSequential(m_transparentSorted, *view.objects, glView, ctx.skinPalette.count());
        drawBatch(ctx, m_batcher, true);
    }
}

void GLForwardPass::drawBatch(GLFrameContext& ctx, const GLInstanceBatcher& batch, bool blended) {
    const GLView& glView = ctx.resources;

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;

    // The opaque batch draws what the occlusion cull showed, while one runs.
    const bool opaque = &batch == &ctx.opaqueBatch;
    const auto submit = [&](const InstanceDraw& draw) {
        if (opaque) {
            ctx.drawOpaque(draw);
        } else {
            batch.draw(ctx.gl, draw);
        }
    };

    for (const InstanceDraw& draw : batch.draws()) {
        // Grouped batches sort skinned runs together; a sequential (transparent)
        // batch may alternate, to keep its depth order.
        const GLMaterial* material = glView.getMaterial(draw.material);
        Vkm::GL::Shader&  program  = m_programs.programFor(material, draw.skinned, opaque);
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
            ctx.gl.setFaceCulling(blended || !material->doubleSided());
        }
        // Blending does not depth-sort a mesh's own faces: the far side first, then the near.
        if (blended && material && material->doubleSided()) {
            ctx.gl.setCullFace(GL_FRONT);
            submit(draw);
            ctx.gl.setCullFace(GL_BACK);
        }
        submit(draw);
    }
    ctx.gl.setFaceCulling(true);
}

void GLForwardPass::drawWireframe(GLFrameContext& ctx, bool posed) {
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    // Toward the eye, which the reversed depth puts at the greater (gl_depth.h).
    ctx.gl.setPolygonOffset(GL_LINE, true, 1.0f, 1.0f);

    forEachProgram(posed, [](Vkm::GL::Shader& program) { program.setUniform1i("u_wirePass", 1); });

    drawBatch(ctx, ctx.opaqueBatch);
    // Still the alpha-masked batch: the transparent one is built after this.
    if (!ctx.alphaMask.empty()) drawBatch(ctx, m_batcher);

    forEachProgram(posed, [](Vkm::GL::Shader& program) { program.setUniform1i("u_wirePass", 0); });

    ctx.gl.setPolygonOffset(GL_LINE, false);
    ctx.gl.setPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    ctx.gl.setBlending(false);
}

void GLForwardPass::bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx) const {
    const RenderView& view = ctx.view;

    // u_hasIBL gates the split-sum ambient in the shader; without a baked
    // environment it falls back to flat ambient.
    bindAmbient(ctx, shader);

    // Every bucket fogs itself at its own depth, so the opaque scene the
    // refraction copies and the transparents blend over is fogged already.
    bindFog(ctx, shader);

    shader.setUniform1i("u_useClusters", 1);
    shader.setUniform1i("u_hardShadows", 0);
    // The baked programs alone read the lightmaps.
    if (shader.hasUniform("u_hasLightmaps")) {
        shader.setUniform1i("u_hasLightmaps", ctx.lightmaps && ctx.lightmaps->any() ? 1 : 0);
    }
    shader.setUniform1i("u_renderMode", static_cast<int>(view.settings.renderMode));
    shader.setUniform1i("u_wirePass", 0);
}

void GLForwardPass::bindBucketUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx, Bucket bucket) const {
    // GTAO saw only what the prepass drew.
    bindAO(ctx, shader, bucket == Bucket::Opaque);
    // The plain and layered programs are built without transmission, so have nothing to refract.
    if (shader.hasUniform("u_hasSceneColor")) {
        shader.setUniform1i("u_hasSceneColor", bucket == Bucket::Transparent ? 1 : 0);
    }
    shader.setUniform1i(
        "u_alphaToCoverage",
        (bucket == Bucket::AlphaMask && ctx.sceneRender.samples() > 1) ? 1 : 0
    );
}

} // namespace Vkm::Engine
