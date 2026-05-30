#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_prepass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "config/gl_config.h"
#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "core/gl_instance_batcher.h"

#include "resource/material_asset.h"
#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLPrepass::GLPrepass(ShaderHandle shader)
    : GLRenderPass("GLPrepass")
    , m_shader(shader)
{
}

bool GLPrepass::enabledForView(const RenderView& view) const {
    if (!isEnabled()) return false;

    // Run iff at least one G-buffer consumer will read the normal/position
    // targets this frame. Each clause mirrors that consumer's own
    // enabledForView so the prepass is never the lone producer of an
    // unread resource (which would also trip the graph's read-before-write
    // check) and never skipped while a consumer still needs it:
    //   GTAO   -> ao.enabled            (also feeds the forward pass AO read)
    //   SSR    -> ssr.enabled        && !disablePost
    //   TAA    -> taa.enabled        && !disablePost
    //   MBlur  -> motionBlur.enabled && !disablePost
    //   DoF    -> dof.enabled        && !disablePost
    //   HiZ    -> occlusion.useHiZ   && !disablePost
    // If a new G-buffer consumer is added, extend this list (or, better,
    // replace it with graph-level dead-pass elimination - see CODE_REVIEW.md #12).
    const auto& env = view.environment;
    const bool postOn = !view.modeConfig.disablePost;
    return env.ao.enabled
        || (postOn && (env.ssr.enabled
                    || env.taa.enabled
                    || env.motionBlur.enabled
                    || env.dof.enabled
                    || env.occlusion.useHiZ));
}

void GLPrepass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // GLGBuffer is owned and resized by GLBackend.
}

void GLPrepass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& glView  = gl.getView();
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    if (!gbuffer.isReady() || view.drawables.empty()) return;

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    gbuffer.bindGeometry();

    auto& ctx = gl.getContext();
    ctx.setClearColor({0.0f, 0.0f, 0.0f, 0.0f});
    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
    ctx.clearColor();
    ctx.clear();

    shader->bind();
    shader->setUniformMatrix4fv("u_view", view.camera.view);
    shader->setUniformMatrix4fv("u_projection", view.camera.projection);

    auto& batcher        = glView.getInstanceBatcher();
    const auto& batches  = batcher.getBatches();

    for (const auto& batch : batches) {
        if (batch.materialType != MaterialType::Opaque) continue;
        if (batch.instanceCount == 0) continue;

        GLMesh* mesh = glView.getMutableMesh(batch.mesh);
        if (!mesh) continue;

        // Material UBO (binding 0) so the prepass can pack roughness/metalness
        // into the G-buffer for per-material SSR.
        if (const GLMaterial* material = glView.getMaterial(batch.material)) {
            material->bind(GLConfig::UBOBindingPoints::Material);
        }

        batcher.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
        mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
    }
}

} // namespace Engine
