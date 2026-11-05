#include "pass/scene/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "storage/light/gl_cluster_grid.h"
#include "storage/light/gl_irradiance_volume.h"
#include "storage/light/gl_lightmaps.h"
#include "storage/light/gl_shadow_atlas.h"
#include "storage/water/gl_wave_fields.h"
#include "storage/water/gl_water_shore.h"
#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "storage/light/gl_ibl.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "core/render_mode.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLForwardPass::GLForwardPass(GLForwardPrograms& programs)
    : m_programs(programs) {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const GLAssetMirror& mirror = ctx.mirror;

    // Camera and lights are bound by the backend. Every bucket writes the reflection inputs
    // while `ssr` is on; the transparent one only dims them.
    ctx.sceneRender.bindForwardPass(ctx.gl);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);
    ctx.gl.setDepthWrite(false);

    // ShadowBlock carries matrices and slots; only the depth textures bind here. The light loop
    // picks a slot from each light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);

    // The irradiance cube and the volume beside these are bindAmbient's. The
    // LUT is integrated at start, so it is there with or without a sky.
    if (ctx.ibl.isReady()) ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);

    ctx.clusters.bind();
    ctx.objects.bind();
    if (ctx.lightmaps) ctx.lightmaps->bindPages();
    // The ripples and the shore, if the frame made them, for the water program alone; a frame
    // with no water never binds it.
    if (ctx.waterShown) {
        ctx.waveFields.bind();
        ctx.waterShore.bind();
        m_programs.water().setUniform4fv("u_shore", ctx.waterShore.area());
        m_programs.water().setUniform1f("u_shoreLevel", ctx.shore.level);
    }

    // With no palette no run is skinned, so the skinned programs are never bound
    // and never told anything.
    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();

    forEachProgram(posed, ctx.waterShown, [&](Vkm::OpenGL::Shader& program) {
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
        forEachProgram(posed, ctx.waterShown, [&](Vkm::OpenGL::Shader& program) {
            bindBucketUniforms(program, ctx, Bucket::AlphaMask);
        });
        m_batcher.buildGrouped(ctx.alphaMask, *view.objects, mirror, ctx.skinPalette.count(), MeshOf::Eye);
        drawBatch(ctx, m_batcher);
        ctx.gl.setAlphaToCoverage(false);
    }

    if (view.display.mode == RenderMode::Wireframe) drawWireframe(ctx, posed);

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

        // Copy the opaque + sky scene for transmissive surfaces to refract, when one is drawn;
        // blitColorFrom resolves the multisample color into the single-sample scratch.
        const auto refracts = [&](uint32_t object) {
            const GLMaterial* material = mirror.getMaterial(view.objects->draws[object].material);
            return material && material->refracts();
        };
        if (std::any_of(ctx.transparent.begin(), ctx.transparent.end(), refracts)) {
            ctx.colorDst->blitColorFrom(ctx.sceneRender, ctx.colorDst->pixels());
            ctx.sceneRender.bindForwardPass(ctx.gl);
            ctx.colorDst->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
        }
        forEachProgram(posed, ctx.waterShown, [&](Vkm::OpenGL::Shader& program) {
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

        m_batcher.buildSequential(
            m_transparentSorted,
            *view.objects,
            mirror,
            ctx.skinPalette.count(),
            MeshOf::Eye
        );
        drawBatch(ctx, m_batcher, true);
    }
}

void GLForwardPass::drawBatch(GLFrameContext& ctx, const GLInstanceBatcher& batch, bool blended) {
    const GLAssetMirror& mirror = ctx.mirror;

    const GLMaterial* boundMaterial = nullptr;
    const Vkm::OpenGL::Shader* boundProgram = nullptr;

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
        const GLMaterial*    material = mirror.getMaterial(draw.material);
        Vkm::OpenGL::Shader& program  = draw.water
            ? m_programs.water()
            : m_programs.programFor(material, draw.skinned, opaque);
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }

        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(mirror);
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
    ctx.gl.setPolygonMode(GL_LINE);
    // Toward the eye, which the reversed depth puts at the greater (gl_depth.h).
    ctx.gl.setPolygonOffset(GL_LINE, true, 1.0f, 1.0f);

    forEachProgram(posed, ctx.waterShown, [](Vkm::OpenGL::Shader& program) {
        program.setUniform1i("u_wirePass", 1);
    });

    drawBatch(ctx, ctx.opaqueBatch);
    // Still the alpha-masked batch: the transparent one is built after this.
    if (!ctx.alphaMask.empty()) drawBatch(ctx, m_batcher);

    forEachProgram(posed, ctx.waterShown, [](Vkm::OpenGL::Shader& program) {
        program.setUniform1i("u_wirePass", 0);
    });

    ctx.gl.setPolygonOffset(GL_LINE, false);
    ctx.gl.setPolygonMode(GL_FILL);
    ctx.gl.setBlending(false);
}

void GLForwardPass::bindFrameUniforms(Vkm::OpenGL::Shader& shader, GLFrameContext& ctx) const {
    const RenderView& view = ctx.view;

    // u_hasIBL gates the split-sum ambient in the shader; without a baked
    // environment it falls back to flat ambient.
    bindAmbient(ctx, shader);

    // Every bucket fogs itself at its own depth, so the opaque scene the
    // refraction copies and the transparents blend over is fogged already.
    bindFog(ctx, shader);

    shader.setUniform1i("u_useClusters", 1);
    // The baked programs alone read the lightmaps.
    if (shader.hasUniform("u_hasLightmaps")) {
        shader.setUniform1i("u_hasLightmaps", ctx.lightmaps && ctx.lightmaps->any() ? 1 : 0);
    }
    shader.setUniform1i("u_renderMode", static_cast<int>(view.display.mode));
    shader.setUniform1i("u_wirePass", 0);
}

void GLForwardPass::bindBucketUniforms(
    Vkm::OpenGL::Shader& shader,
    GLFrameContext& ctx,
    Bucket bucket
) const {
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
