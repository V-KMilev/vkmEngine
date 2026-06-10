#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_forward_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "resource/gl_material.h"
#include "resource/gl_mesh.h"
#include "system/render/render_view.h"

namespace Engine {

GLForwardPass::GLForwardPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/forward")) {}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;

    // Camera + light UBOs are already uploaded and bound by the backend; this
    // pass renders the lit geometry into the HDR target.
    ctx.sceneHDR.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setBlending(false);
    ctx.gl.setClearColor({0.01f, 0.01f, 0.01f, 1.0f});
    ctx.gl.clear(true, true, false);

    m_shader->bind();

    // Partition by material type. Opaque / AlphaMask / Unlit keep the view's
    // order (sorted upstream by material+mesh); Transparent is pulled aside
    // and sorted back-to-front so alpha blending composes correctly.
    m_opaque.clear();
    m_transparent.clear();
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) {
            const glm::vec3 toCam = view.camera.position - glm::vec3(d.model[3]);
            m_transparent.emplace_back(glm::dot(toCam, toCam), &d);
        } else {
            m_opaque.push_back(&d);
        }
    }

    drawList(ctx, m_opaque);

    if (!m_transparent.empty()) {
        std::sort(m_transparent.begin(), m_transparent.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        m_transparentSorted.clear();
        m_transparentSorted.reserve(m_transparent.size());
        for (const auto& entry : m_transparent) m_transparentSorted.push_back(entry.second);

        // Blended, depth-tested against the opaque scene but not written, so
        // transparent surfaces never occlude each other in the depth buffer.
        ctx.gl.setBlending(true);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.gl.setDepthWrite(false);

        drawList(ctx, m_transparentSorted);

        ctx.gl.setBlending(false);
        ctx.gl.setDepthWrite(true);
    }

    ctx.gl.setFaceCulling(false);
}

void GLForwardPass::drawList(GLFrameContext& ctx, const std::vector<const DrawableData*>& list) {
    const GLView& glView = ctx.resources;

    // Re-bind material state only when it differs from the last drawable's.
    const GLMaterial* boundMaterial = nullptr;

    for (const DrawableData* d : list) {
        const GLMesh* mesh = glView.getMesh(d->mesh);
        if (!mesh) continue;

        const GLMaterial* material = glView.getMaterial(d->material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::Material);
            material->bindTextures(glView);

            // Single-sided materials cull back faces; doubleSided shades
            // them (the shader flips the normal via gl_FrontFacing).
            ctx.gl.setFaceCulling(!material->isDoubleSided());
            if (!material->isDoubleSided()) ctx.gl.setCullFace(GL_BACK);

            boundMaterial = material;
        }

        m_shader->setUniformMatrix4fv("u_model", d->model);
        mesh->draw();
    }
}

} // namespace Engine
