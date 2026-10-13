#include "pass/gl_particle_pass.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_stream_ring.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "gl_screen_triangle.h"
#include "storage/gl_shadow_atlas.h"
#include "storage/gl_cluster_grid.h"
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

void GLParticlePass::sortAdditive(const RenderView& view) {
    // By texture, a draw each; the blend adds in any order.
    m_sorted = view.particlesAdditive;
    std::stable_sort(m_sorted.begin(), m_sorted.end(), [](const ParticleData& a, const ParticleData& b) {
        return a.params.z < b.params.z;
    });
}

void GLParticlePass::bindTexture(GLFrameContext& ctx, float texture) {
    // 0 is the soft disc; a texture loading or failed shows the missing checker, as a map would.
    const auto index = static_cast<size_t>(texture);
    m_shader.setUniform1i("u_textured", index > 0 ? 1 : 0);
    if (index == 0 || index > ctx.view.particleTextures.size()) return;
    const Vkm::GL::Texture2D* sprite = ctx.resources.getTexture(ctx.view.particleTextures[index - 1]);
    if (!sprite) sprite = &ctx.resources.missingTexture();
    sprite->bindSlot(GLBindings::ParticleTextureSlots::SPRITE);
}

void GLParticlePass::drawBatch(GLFrameContext& ctx, const std::vector<ParticleData>& batch) {
    if (batch.empty()) return;

    const uint32_t count = static_cast<uint32_t>(batch.size());
    const uint32_t bytes = count * static_cast<uint32_t>(sizeof(ParticleData));

    // Into the frame's stream: the alpha batch does not wait on the additive one's draws.
    const Vkm::GL::BufferSpan instances = ctx.resources.stream().write(batch.data(), bytes);
    Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::PARTICLES, instances);

    // Attribute-less: per-particle data is read from the SSBO by gl_InstanceID plus the run's
    // first, and core profile still wants a VAO bound to draw. A run per texture in a row.
    ctx.screenTri.bind();
    for (uint32_t first = 0; first < count;) {
        uint32_t end = first + 1;
        while (end < count && batch[end].params.z == batch[first].params.z) ++end;
        bindTexture(ctx, batch[first].params.z);
        m_shader.setUniform1i("u_firstInstance", static_cast<int>(first));
        VKM_GL_CHECK(glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(end - first)));
        first = end;
    }
}

void GLParticlePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.particlesAdditive.empty() && view.particlesAlpha.empty()) return;

    // The depth the particles fade against, a copy: the target's own is attached as they draw.
    const auto width  = static_cast<uint32_t>(view.sceneWidth());
    const auto height = static_cast<uint32_t>(view.sceneHeight());
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

    // An alpha-blended particle is lit as it draws: the lights by its cluster, through their
    // shadows' hard lookups, which compare and never read the raw depth, and the environment
    // (shaders/particle/vertex.shader).
    ctx.clusters.bind();
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);
    bindAmbient(ctx, m_shader);

    // Additive first (its blend is commutative), then alpha far to near. u_additive also tells
    // the shader how to fog each half.
    if (!view.particlesAdditive.empty()) {
        sortAdditive(view);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
        m_shader.setUniform1i("u_additive", 1);
        drawBatch(ctx, m_sorted);
    }
    if (!view.particlesAlpha.empty()) {
        sortAlpha(view);
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader.setUniform1i("u_additive", 0);
        drawBatch(ctx, m_sorted);
    }
    setReflectRoughnessWritable(true);
}

} // namespace Vkm::Engine
