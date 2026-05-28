#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_outline_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLOutlinePass::enabledForView(const RenderView& view) const {
    if (!isEnabled() || !view.environment.selection.enabled) return false;
    for (const auto& d : view.drawables) {
        if (d.selected) return true;
    }
    return false;
}

GLOutlinePass::GLOutlinePass(ShaderHandle shader)
    : RenderPass("GLOutlinePass")
    , m_shader(shader)
{}

void GLOutlinePass::onResize(RenderBackend&, uint32_t, uint32_t) {}

void GLOutlinePass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLOutlinePass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    if (!view.environment.selection.enabled) return;

    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    if (!hdr.isReady()) return;

    auto& gl     = static_cast<GLBackend&>(backend);
    auto& glView = gl.getView();
    auto& ctx    = gl.getContext();

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    // Route writes to the overlay attachment; stencil lives on the shared
    // depth/stencil renderbuffer.
    hdr.bindForOverlay();

    // Zero stencil once, before any draws. The GL 3.0+ per-buffer clear
    // (instead of glClear(GL_STENCIL_BUFFER_BIT)) avoids a class of driver
    // bugs where a mask-bit clear on a packed DEPTH24_STENCIL8 RT with
    // mixed-NONE drawBuffers ends up touching color attachment 1 as well.
    ctx.setStencilMask(0xFF);
    constexpr GLint zero = 0;
    glClearBufferiv(GL_STENCIL, 0, &zero);

    ctx.setStencilTest(true);
    ctx.setFaceCulling(false);
    ctx.setBlending(false);
    // Depth test off for both phases. The forward pass already wrote this
    // mesh's depth values; with GL_LESS the second draw would self-occlude
    // (new depth == existing depth fails) and stencil would stay 0 - the
    // outline phase would then bleed across the whole mesh. Ignoring depth
    // also keeps the selection visible through occluders, the convention
    // editors like Unity / Unreal follow.
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);

    shader->bind();

    shader->setUniform3fv("u_color", view.environment.selection.color);
    shader->setUniform1f("u_thickness", view.environment.selection.thickness);
    shader->setUniform2f(
        "u_viewportSize",
        static_cast<float>(view.viewportWidth),
        static_cast<float>(view.viewportHeight));

    for (const auto& drawable : view.drawables) {
        if (!drawable.selected) continue;
        GLMesh* mesh = glView.getMutableMesh(drawable.mesh);
        if (!mesh) continue;

        // Non-instanced draw: model matrix as a uniform, so we never touch
        // the VAO's per-instance attribute slots (4-7). GLInstanceBatcher
        // caches per-VAO ownership of those slots; if we rebound them, the
        // batcher would skip its next-frame re-attach and the forward pass
        // would draw the selected mesh's instances from our stale buffer.
        shader->setUniformMatrix4fv("u_model", drawable.model);

        // Phase A: stamp the mesh's screen coverage into stencil. No colour
        // writes; stencil = 1 wherever any triangle covers a sample.
        ctx.setColorMask(false, false, false, false);
        ctx.setStencilFunc(GL_ALWAYS, 1, 0xFF);
        ctx.setStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        ctx.setStencilMask(0xFF);
        shader->setUniform1f("u_inflate", 0.0f);
        mesh->draw(GL_TRIANGLES);

        // Phase B: re-draw with vertices pushed outward in screen space.
        // Stencil read-only, NOTEQUAL 1 keeps only the inflated band.
        ctx.setColorMask(true, true, true, true);
        ctx.setStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        ctx.setStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        ctx.setStencilMask(0x00);
        shader->setUniform1f("u_inflate", 1.0f);
        mesh->draw(GL_TRIANGLES);
    }

    // Restore state for downstream passes. Stencil test off by default
    // everywhere else; mask back to 0xFF so a subsequent stencil clear
    // works as expected.
    ctx.setStencilTest(false);
    ctx.setStencilMask(0xFF);
    ctx.setDepthWrite(true);
}

} // namespace Engine
