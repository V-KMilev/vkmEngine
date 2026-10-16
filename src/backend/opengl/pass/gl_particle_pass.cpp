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
#include "ecs/component/render/particle_emitter.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLParticlePass::GLParticlePass()
    : m_shader("shaders/particle")
    , m_strip("shaders/particle_strip") {}

GLParticlePass::~GLParticlePass() = default;

template <typename PositionOf>
void GLParticlePass::orderByDepth(const RenderView& view, uint32_t count, PositionOf&& positionOf) {
    // By view depth, not distance: the quads lie in planes facing the view, and by distance a
    // pair near the screen's edge sorts the wrong way round under a wide field of view.
    const glm::vec3 eye     = view.camera.position;
    const glm::vec3 forward = glm::vec3(view.camera.invView[2]) * -1.0f;
    m_order.resize(count);
    for (uint32_t i = 0; i < count; ++i) m_order[i] = { glm::dot(positionOf(i) - eye, forward), i };
    std::sort(m_order.begin(), m_order.end(), [](const DepthOrder& a, const DepthOrder& b) {
        return a.depth > b.depth;
    });
}

void GLParticlePass::sortBillboards(
    const RenderView& view,
    const std::vector<ParticleData>& particles,
    bool byDepth
) {
    if (!byDepth) {
        // By texture, a draw each; the blend comes out the same in any order.
        m_sorted = particles;
        std::stable_sort(m_sorted.begin(), m_sorted.end(), [](const ParticleData& a, const ParticleData& b) {
            return a.params.z != b.params.z ? a.params.z < b.params.z : a.uvNext.w < b.uvNext.w;
        });
        return;
    }

    PROFILE_SCOPE("Particles/Sort");
    const auto at = [&](uint32_t i) { return glm::vec3(particles[i].positionSize); };
    orderByDepth(view, static_cast<uint32_t>(particles.size()), at);

    m_sorted.resize(particles.size());
    for (size_t i = 0; i < m_order.size(); ++i) m_sorted[i] = particles[m_order[i].index];
}

void GLParticlePass::bindTexture(
    GLFrameContext& ctx,
    Vkm::GL::Shader& shader,
    float texture,
    float normalMap
) {
    // 0 is none; a texture loading or failed shows the missing checker, as a map would.
    const auto bind = [&](float place, const char* flag, uint32_t slot) {
        const auto index = static_cast<size_t>(place);
        shader.setUniform1i(flag, index > 0 ? 1 : 0);
        if (index == 0 || index > ctx.view.particleTextures.size()) return;
        const Vkm::GL::Texture2D* image = ctx.resources.getTexture(ctx.view.particleTextures[index - 1]);
        if (!image) image = &ctx.resources.missingTexture();
        image->bindSlot(slot);
    };
    bind(texture, "u_textured", GLBindings::ParticleTextureSlots::SPRITE);
    if (&shader == &m_shader) bind(normalMap, "u_normalMapped", GLBindings::ParticleTextureSlots::NORMAL);
}

void GLParticlePass::drawBillboards(GLFrameContext& ctx, const std::vector<ParticleData>& batch) {
    if (batch.empty()) return;

    const uint32_t count = static_cast<uint32_t>(batch.size());
    const uint32_t bytes = count * static_cast<uint32_t>(sizeof(ParticleData));

    // Into the frame's stream: one blend's batch does not wait on another's draws.
    const Vkm::GL::BufferSpan instances = ctx.resources.stream().write(batch.data(), bytes);
    Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::PARTICLES, instances);

    // Attribute-less: per-particle data is read from the SSBO by gl_InstanceID plus the run's
    // first, and core profile still wants a VAO bound to draw. A run per texture in a row.
    m_shader.bind();
    ctx.screenTri.bind();
    for (uint32_t first = 0; first < count;) {
        uint32_t end = first + 1;
        while (end < count && batch[end].params.z == batch[first].params.z
               && batch[end].uvNext.w == batch[first].uvNext.w) {
            ++end;
        }
        bindTexture(ctx, m_shader, batch[first].params.z, batch[first].uvNext.w);
        m_shader.setUniform1i("u_firstInstance", static_cast<int>(first));
        VKM_GL_CHECK(glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(end - first)));
        first = end;
    }
}

void GLParticlePass::drawStrips(GLFrameContext& ctx, const std::vector<ParticleStrip>& strips, bool byDepth) {
    if (strips.empty()) return;
    const RenderView& view = ctx.view;

    m_strips.clear();
    if (byDepth) {
        // By its head, the end nearest the emitter: a strip is one piece, not one a point.
        const auto head = [&](uint32_t i) {
            return glm::vec3(view.stripPoints[strips[i].first].positionWidth);
        };
        orderByDepth(view, static_cast<uint32_t>(strips.size()), head);
        for (const DepthOrder& order : m_order) m_strips.push_back(strips[order.index]);
    } else {
        m_strips = strips;
        const auto byTexture = [](const auto& a, const auto& b) { return a.texture < b.texture; };
        std::stable_sort(m_strips.begin(), m_strips.end(), byTexture);
    }

    // Two vertices a point, numbered from the frame's first point: the vertex stage reads its run.
    m_strip.bind();
    ctx.screenTri.bind();
    for (size_t first = 0; first < m_strips.size();) {
        size_t end = first;
        m_firsts.clear();
        m_counts.clear();
        while (end < m_strips.size() && m_strips[end].texture == m_strips[first].texture) {
            m_firsts.push_back(static_cast<int32_t>(m_strips[end].first * 2));
            m_counts.push_back(static_cast<int32_t>(m_strips[end].count * 2));
            ++end;
        }
        bindTexture(ctx, m_strip, m_strips[first].texture);
        VKM_GL_CHECK(
            glMultiDrawArrays(
                GL_TRIANGLE_STRIP,
                m_firsts.data(),
                m_counts.data(),
                static_cast<GLsizei>(m_firsts.size())
            )
        );
        first = end;
    }
}

void GLParticlePass::drawBlend(
    GLFrameContext& ctx,
    ParticleBlend blend,
    const std::vector<ParticleData>& billboards,
    const std::vector<ParticleStrip>& strips
) {
    if (billboards.empty() && strips.empty()) return;
    const bool additive = blend == ParticleBlend::Additive;
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    m_shader.setUniform1i("u_blend", static_cast<int>(blend));
    m_strip.setUniform1i("u_blend", static_cast<int>(blend));

    sortBillboards(ctx.view, billboards, !additive);
    drawBillboards(ctx, m_sorted);
    drawStrips(ctx, strips, !additive);
}

void GLParticlePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const bool billboards = !view.particlesAdditive.empty() || !view.particlesAlpha.empty()
        || !view.particlesDistortion.empty();
    if (!billboards && view.stripPoints.empty()) return;

    // The depth the particles fade against, a copy: the target's own is attached as they draw.
    const auto width  = static_cast<uint32_t>(view.sceneWidth());
    const auto height = static_cast<uint32_t>(view.sceneHeight());
    m_depthCopy.resize(width, height, 1, false);
    m_depthCopy.blitDepthFrom(ctx.sceneRender);
    m_depthCopy.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);

    // Every strip's points, once a frame.
    if (!view.stripPoints.empty()) {
        const auto bytes = static_cast<uint32_t>(view.stripPoints.size() * sizeof(StripPoint));
        const Vkm::GL::BufferSpan points = ctx.resources.stream().write(view.stripPoints.data(), bytes);
        Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::STRIP_POINTS, points);
    }

    // Depth-tested, never depth-writing. Into the reflection inputs too: a particle hides the
    // reflection behind it as it hides the surface (shaders/particle/shade.glsl).
    ctx.sceneRender.bindForwardPass(ctx.gl);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    setReflectRoughnessWritable(false);

    for (Vkm::GL::Shader* program : {&m_shader, &m_strip}) {
        program->bind();
        bindFog(ctx, *program);
        bindAmbient(ctx, *program);
    }

    // An alpha-blended particle is lit as it draws: the lights by its cluster, through their
    // shadows' hard lookups, which compare and never read the raw depth, and the environment
    // (shaders/particle/light.glsl).
    ctx.clusters.bind();
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);

    // Additive first (its blend is commutative), then alpha far to near, then what bends the
    // scene as all of them left it.
    drawBlend(ctx, ParticleBlend::Additive, view.particlesAdditive, view.stripsAdditive);
    drawBlend(ctx, ParticleBlend::Alpha, view.particlesAlpha, view.stripsAlpha);
    if (!view.particlesDistortion.empty() || !view.stripsDistortion.empty()) {
        // Into the post chain's free scratch, as the forward pass's refraction does: nothing
        // reads it until the post passes draw over it.
        ctx.colorDst->blitColorFrom(ctx.sceneRender);
        ctx.colorDst->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
        ctx.sceneRender.bindForwardPass(ctx.gl);  // the blit left the scratch bound
        drawBlend(ctx, ParticleBlend::Distortion, view.particlesDistortion, view.stripsDistortion);
    }
    setReflectRoughnessWritable(true);
}

} // namespace Vkm::Engine
