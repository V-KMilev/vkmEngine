#include "gl_prepass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"

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
    : RenderPass("GLPrepass")
    , m_shader(shader)
{
}

void GLPrepass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLGBuffer is owned and resized by GLBackend.
}

void GLPrepass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLPrepass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl      = static_cast<GLBackend&>(backend);
    auto& glView  = gl.getView();
    auto& gbuffer = gl.getGBuffer();
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
