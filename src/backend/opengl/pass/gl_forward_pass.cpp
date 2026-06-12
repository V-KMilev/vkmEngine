#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_ao_target.h"
#include "data/gl_shadow_atlas.h"
#include "gl_view.h"
#include "core/engine_config.h"
#include "convention/gl_bindings.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "data/gl_ibl.h"
#include "system/render/render_view.h"

namespace Engine {

GLForwardPass::GLForwardPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/forward/pbr")) {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Camera + light UBOs are already uploaded and bound by the backend; this
    // pass renders the lit geometry into the HDR target.
    ctx.sceneHDR.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(true);
    ctx.gl.setCullFace(GL_BACK);

    if (ctx.depthPrimed) {
        // The prepass cleared the target and primed opaque depth, and the skybox
        // filled the background before this pass. Match the primed depth with
        // LEQUAL and leave writes off for early-Z. Do NOT clear here - that would
        // wipe the skybox and leave transparents nothing to blend over.
        ctx.gl.setDepthFunc(GL_LEQUAL);
        ctx.gl.setDepthWrite(false);
    } else {
        // No prepass (fallback): single-pass forward owns its depth + colour
        // clear. The standard pipeline always runs the prepass, so this path is
        // not normally taken (and a skybox, if present, would be cleared here).
        ctx.gl.setDepthFunc(GL_LESS);
        ctx.gl.setDepthWrite(true);
        ctx.gl.setClearColor({0.01f, 0.01f, 0.01f, 1.0f});
        ctx.gl.clear(true, true, false);
    }

    m_shader->bind();

    // Shadows: the ShadowBlock UBO (binding 3) carries the matrices + slots; here
    // we only bind the depth textures. The light loop samples per light type via
    // each light's shadowSlot (GpuLight.spot.w).
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::Atlas2D);
    for (uint32_t s = 0; s < Config::MAX_SHADOW_CASTERS_CUBE; ++s) {
        ctx.shadowAtlas.bindCube(s, GLBindings::ShadowTextureSlots::CubeBase + s);
    }

    // IBL: bind the baked product set when present; u_hasIBL gates the split-sum
    // ambient in the shader (flat-ambient fallback when no environment baked).
    const bool hasIBL = ctx.ibl.isReady();
    if (hasIBL) {
        ctx.ibl.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
        ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
        ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
    }
    m_shader->setUniform1i("u_hasIBL", hasIBL ? 1 : 0);

    // Screen-space AO from the GTAO pass: bind + gate it. The shader multiplies
    // it into the indirect term (ambient/IBL). Absent (pass disabled) -> 1.0.
    if (ctx.aoReady) ctx.ao.bindTexture(GLBindings::PostTextureSlots::SSAO);
    m_shader->setUniform1i("u_hasSSAO", ctx.aoReady ? 1 : 0);

    // Reflection probes: the backend bound the cube arrays + ProbeBlock UBO; hand
    // the shader the active count, or 0 when probes are toggled off.
    m_shader->setUniform1i("u_probeCount", ctx.view.settings.probes ? ctx.probeCount : 0);

    // Refraction is sampled only by the transparent bucket; default it off here
    // and switch it on after the scene-colour copy below.
    m_shader->setUniform1i("u_hasSceneColor", 0);
    m_shader->setUniform2f("u_screenSize", static_cast<float>(view.viewportWidth), static_cast<float>(view.viewportHeight));

    // Opaque / AlphaMask / Unlit (already split out by the backend) keep the
    // view's order - sorted upstream by material+mesh - and merge into instanced
    // runs grouped by (material, mesh).
    drawRuns(ctx, m_batcher.buildGrouped(ctx.opaque, glView));

    if (!ctx.transparent.empty()) {
        // Key the transparent bucket by squared distance and sort back-to-front
        // so alpha blending composes correctly.
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
        // is behind them, then resume rendering into the HDR target.
        ctx.sceneColor.blitColorFrom(ctx.sceneHDR);
        ctx.sceneHDR.bind(ctx.gl);
        ctx.sceneColor.bindColor(GLBindings::PostTextureSlots::SceneColor);
        m_shader->setUniform1i("u_hasSceneColor", 1);

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        drawRuns(ctx, m_batcher.buildSequential(m_transparentSorted, glView));

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

void GLForwardPass::drawRuns(GLFrameContext& ctx, const std::vector<InstanceRun>& runs) {
    const GLView& glView = ctx.resources;

    // Re-bind material state only when it differs from the last run's.
    const GLMaterial* boundMaterial = nullptr;

    for (const InstanceRun& run : runs) {
        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::Material);
            material->bindTextures(glView);
            boundMaterial = material;
        }
        m_batcher.drawRun(run);
    }
}

} // namespace Engine
