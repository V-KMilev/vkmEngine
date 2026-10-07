#include "pass/gl_particle_pass.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_shader_storage_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "gl_screen_triangle.h"
#include "frame/gl_stream_upload.h"
#include "debug/profiler.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLParticlePass::GLParticlePass()
    : m_shader("shaders/particle") {}

GLParticlePass::~GLParticlePass() = default;

void GLParticlePass::sortAlpha(const RenderView& view) {
    PROFILE_SCOPE("Particles/Sort");
    const std::vector<ParticleData>& particles = view.particlesAlpha;
    const glm::vec3 eye     = view.camera.position;
    const glm::vec3 forward = glm::vec3(view.camera.invView[2]) * -1.0f;

    // By view depth, not distance: the quads lie in planes facing the view, and by distance a
    // pair near the screen's edge sorts the wrong way round under a wide field of view.
    m_order.resize(particles.size());
    for (uint32_t i = 0; i < particles.size(); ++i) {
        m_order[i] = { glm::dot(glm::vec3(particles[i].positionSize) - eye, forward), i };
    }
    std::sort(
        m_order.begin(),
        m_order.end(),
        [](const ParticleOrder& a, const ParticleOrder& b) { return a.depth > b.depth; }
    );

    m_sorted.resize(particles.size());
    for (size_t i = 0; i < m_order.size(); ++i) m_sorted[i] = particles[m_order[i].index];
}

void GLParticlePass::drawBatch(const std::vector<ParticleData>& batch, const ScreenTriangle& emptyVao) {
    if (batch.empty()) return;

    const uint32_t count = static_cast<uint32_t>(batch.size());
    const uint32_t bytes = count * static_cast<uint32_t>(sizeof(ParticleData));

    // Grown with headroom: sized exactly, a filling emitter would reallocate every frame.
    growAndUpload(m_instances, m_capacity, batch.data(), bytes);
    m_instances->bindBase(GLBindings::SSBOBindingPoints::PARTICLES);

    // Attribute-less: per-particle data is read from the SSBO by gl_InstanceID,
    // and core profile still wants a VAO bound to draw.
    emptyVao.bind();
    VKM_GL_CHECK(glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count)));
}

void GLParticlePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.particlesAdditive.empty() && view.particlesAlpha.empty()) return;

    // The depth the particles fade against, a copy: the target's own is attached as they draw.
    const auto width  = static_cast<uint32_t>(view.viewportWidth);
    const auto height = static_cast<uint32_t>(view.viewportHeight);
    m_depthCopy.resize(width, height, 1, false);
    m_depthCopy.blitDepthFrom(ctx.sceneRender);
    m_depthCopy.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);

    // Depth-tested, never depth-writing. Into the reflection inputs too: a particle hides the
    // reflection behind it as it hides the surface (shaders/particle/fragment.shader).
    ctx.sceneRender.bindForwardPass(ctx.gl);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    setReflectRoughnessWritable(false);

    m_shader.bind();
    bindFog(ctx, m_shader);

    // Additive first (its blend is commutative), then alpha far to near. u_additive also tells
    // the shader how to fog each half.
    if (!view.particlesAdditive.empty()) {
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
        m_shader.setUniform1i("u_additive", 1);
        drawBatch(view.particlesAdditive, ctx.screenTri);
    }
    if (!view.particlesAlpha.empty()) {
        sortAlpha(view);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader.setUniform1i("u_additive", 0);
        drawBatch(m_sorted, ctx.screenTri);
    }
    setReflectRoughnessWritable(true);
}

} // namespace Vkm::Engine
