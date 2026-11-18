#include "pass/scene/gl_particle_pass.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/buffer/stream_ring.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/texture/texture.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "gl_screen_triangle.h"
#include "storage/light/gl_shadow_atlas.h"
#include "storage/light/gl_cluster_grid.h"
#include "base/math/aabb.h"
#include "base/profile/profiler.h"
#include "ecs/component/effect/particle_emitter.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// How many times over the alpha billboards in view would cover the scene, each as a square of its
/// width: what drawing them at half resolution saves, against its target's own draws.
float screensCovered(const RenderView& view) {
    const CameraData& eye = view.camera;
    const float xScale = eye.projection[0][0];
    const float yScale = eye.projection[1][1];
    const float xReach = std::sqrt(1.0f + xScale * xScale);
    const float yReach = std::sqrt(1.0f + yScale * yScale);
    const float pixels = yScale * static_cast<float>(view.sceneHeight());
    float covered = 0.0f;
    for (const ParticleData& p : view.lists.particlesAlpha) {
        const glm::vec3 at = glm::vec3(eye.view * glm::vec4(glm::vec3(p.positionSize), 1.0f));
        const float radius = p.positionSize.w;
        const float depth  = -at.z;
        const bool shown   = depth + radius > eye.zNear
            && std::abs(at.x) * xScale - depth <= radius * xReach
            && std::abs(at.y) * yScale - depth <= radius * yReach;
        if (!shown) continue;
        const float side = radius * pixels / std::max(depth, radius);
        covered += side * side;
    }
    return covered / static_cast<float>(std::max(view.sceneWidth() * view.sceneHeight(), 1u));
}

/**
 * @brief Grow @p box to hold every billboard of @p particles drawn through the eye's projection.
 *
 * @return Whether any is drawn through the viewmodel's projection instead, which @p box leaves out.
 */
bool hold(Math::AABB& box, const std::vector<ParticleData>& particles) {
    bool layered = false;
    for (const ParticleData& p : particles) {
        layered = layered || p.params.w > 0.5f;
        if (p.params.w <= 0.5f) box.expand(ParticleData::reachOf(p));
    }
    return layered;
}

/**
 * @brief Grow @p box to hold every point of @p strips among @p points drawn through the eye's
 *        projection.
 *
 * @return Whether any is drawn through the viewmodel's projection instead, which @p box leaves out.
 */
bool hold(Math::AABB& box, const std::vector<ParticleStrip>& strips, const std::vector<StripPoint>& points) {
    bool layered = false;
    for (const ParticleStrip& strip : strips) {
        for (uint32_t i = strip.first; i < strip.first + strip.count; ++i) {
            layered = layered || points[i].look.z > 0.5f;
            if (points[i].look.z <= 0.5f) box.expand(StripPoint::reachOf(points[i]));
        }
    }
    return layered;
}

/**
 * @brief How far from its pixel the frame's strongest bend reads, as a share of the screen's height.
 *
 * A bend's direction is at most GLParticlePass::BEND_REACH long, times its strength.
 */
float bendReach(const RenderView& view) {
    float most = 0.0f;
    for (const ParticleData& p : view.lists.particlesDistortion) most = std::max(most, std::abs(p.trail.w));
    for (const ParticleStrip& strip : view.lists.stripsDistortion) {
        for (uint32_t i = strip.first; i < strip.first + strip.count; ++i) {
            most = std::max(most, std::abs(view.lists.stripPoints[i].look.y));
        }
    }
    return GLParticlePass::BEND_REACH * most;
}

/**
 * @brief The pixels of @p target the world box @p box covers through @p eye, grown by @p pad.
 *
 * All of them for a box reaching behind the eye; none for an empty one.
 */
GLTarget::PixelBox pixelsOf(const Math::AABB& box, const CameraData& eye, const GLTarget& target, int pad) {
    const GLTarget::PixelBox all = target.pixels();
    if (box.min.x > box.max.x) return {};
    glm::vec2 low(std::numeric_limits<float>::max());
    glm::vec2 high(-std::numeric_limits<float>::max());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 at(
            (corner & 1) ? box.max.x : box.min.x,
            (corner & 2) ? box.max.y : box.min.y,
            (corner & 4) ? box.max.z : box.min.z
        );
        const glm::vec4 clip = eye.viewProjection * glm::vec4(at, 1.0f);
        if (clip.w <= glm::epsilon<float>()) return all;
        low  = glm::min(low, glm::vec2(clip) / clip.w);
        high = glm::max(high, glm::vec2(clip) / clip.w);
    }
    const glm::vec2  size(all.high);
    const glm::ivec2 from = glm::ivec2(glm::floor((low * 0.5f + 0.5f) * size)) - pad;
    const glm::ivec2 to   = glm::ivec2(glm::ceil((high * 0.5f + 0.5f) * size)) + pad;
    return {glm::clamp(from, all.low, all.high), glm::clamp(to, all.low, all.high)};
}

} // namespace

GLParticlePass::GLParticlePass(GLCamera& camera)
    : m_shader("shaders/opengl/scene/particle")
    , m_strip("shaders/opengl/scene/particle/strip")
    , m_halfDepth("shaders/opengl/scene/particle/half_depth")
    , m_composite("shaders/opengl/scene/particle/composite")
    , m_camera(camera) {}

GLParticlePass::~GLParticlePass() = default;

template <typename PositionOf>
void GLParticlePass::orderByDepth(const CameraData& eye, uint32_t count, PositionOf&& positionOf) {
    // By view depth, not distance: the quads lie in planes facing the view, and by distance a
    // pair near the screen's edge sorts the wrong way round under a wide field of view.
    const glm::vec3 forward = glm::vec3(eye.invView[2]) * -1.0f;
    m_order.resize(count);
    for (uint32_t i = 0; i < count; ++i) m_order[i] = { glm::dot(positionOf(i) - eye.position, forward), i };
    std::sort(m_order.begin(), m_order.end(), [](const DepthOrder& a, const DepthOrder& b) {
        return a.depth > b.depth;
    });
}

void GLParticlePass::sortBillboards(
    const CameraData& eye,
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
    orderByDepth(eye, static_cast<uint32_t>(particles.size()), at);

    m_sorted.resize(particles.size());
    for (size_t i = 0; i < m_order.size(); ++i) m_sorted[i] = particles[m_order[i].index];
}

bool GLParticlePass::bindTexture(
    GLFrameContext& ctx,
    Vkm::OpenGL::Shader& shader,
    float texture,
    float normalMap
) {
    // 0 is none. A failed texture shows the missing checker, as a map would; one still loading
    // answers false, since a checker square in a puff of smoke would flash for the frames it takes.
    const auto bind = [&](float place, const char* flag, uint32_t slot) {
        const auto index = static_cast<size_t>(place);
        const bool listed = index > 0 && index <= ctx.view.lists.particleTextures.size();
        const TextureHandle handle = listed ? ctx.view.lists.particleTextures[index - 1] : TextureHandle{};
        const Vkm::OpenGL::Texture2D* image = listed ? ctx.mirror.getTexture(handle) : nullptr;
        if (listed && !image && ctx.mirror.textureFailed(handle)) image = &ctx.mirror.missingTexture();
        shader.setUniform1i(flag, image ? 1 : 0);
        if (image) image->bindSlot(slot);
        return image || !listed;
    };
    if (!bind(texture, "u_textured", GLBindings::ParticleTextureSlots::SPRITE)) return false;
    // A normal map still loading is left out: the run is lit as a ball until it comes.
    if (&shader == &m_shader) bind(normalMap, "u_normalMapped", GLBindings::ParticleTextureSlots::NORMAL);
    return true;
}

void GLParticlePass::drawBillboards(GLFrameContext& ctx, const std::vector<ParticleData>& batch) {
    if (batch.empty()) return;

    const uint32_t count = static_cast<uint32_t>(batch.size());
    const uint32_t bytes = count * static_cast<uint32_t>(sizeof(ParticleData));

    // Into the frame's stream: one blend's batch does not wait on another's draws.
    const Vkm::OpenGL::BufferSpan instances = ctx.mirror.stream().write(batch.data(), bytes);
    Vkm::OpenGL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::PARTICLES, instances);

    // Attribute-less: per-particle data is read from the SSBO by gl_InstanceID plus the run's
    // first, and core profile still wants a VAO bound to draw. A run per texture and normal map in a
    // row.
    m_shader.bind();
    ctx.screenTri.bind();
    for (uint32_t first = 0; first < count;) {
        uint32_t end = first + 1;
        while (end < count && batch[end].params.z == batch[first].params.z
            && batch[end].uvNext.w == batch[first].uvNext.w) {
            ++end;
        }
        if (bindTexture(ctx, m_shader, batch[first].params.z, batch[first].uvNext.w)) {
            m_shader.setUniform1i("u_firstInstance", static_cast<int>(first));
            const auto run = static_cast<int32_t>(end - first);
            ctx.screenTri.array().drawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, run);
        }
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
            return glm::vec3(view.lists.stripPoints[strips[i].first].positionWidth);
        };
        orderByDepth(view.camera, static_cast<uint32_t>(strips.size()), head);
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
        if (bindTexture(ctx, m_strip, m_strips[first].texture)) {
            const auto runs = static_cast<int32_t>(m_firsts.size());
            ctx.screenTri.array().multiDrawArrays(GL_TRIANGLE_STRIP, m_firsts.data(), m_counts.data(), runs);
        }
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
    // Premultiplied, every blend alike: an additive particle covers nothing
    // (shaders/opengl/scene/particle/shade.glsl).
    ctx.gl.setBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_shader.setUniform1i("u_blend", static_cast<int>(blend));
    m_strip.setUniform1i("u_blend", static_cast<int>(blend));

    sortBillboards(ctx.view.camera, billboards, !additive);
    drawBillboards(ctx, m_sorted);
    drawStrips(ctx, strips, !additive);
}

void GLParticlePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const bool billboards  = !view.lists.particlesAdditive.empty() || !view.lists.particlesAlpha.empty()
        || !view.lists.particlesDistortion.empty();

    // Sized and its camera block filled every frame, particles or not, so they are made with the
    // scene's size: never on the frame a particle first shows. The frame's camera bound again.
    const glm::uvec2 size(view.sceneWidth(), view.sceneHeight());
    const glm::uvec2 half = (size + 1u) / 2u;
    m_depthCopy.resize(ctx.gl, size.x, size.y, 1, false);
    m_half.resize(ctx.gl, half.x, half.y, 1, false);
    m_halfDepthCopy.resize(ctx.gl, half.x, half.y, 1, false);
    m_halfCamera.update(view.camera, glm::vec2(half));
    m_camera.update(view.camera, glm::vec2(size));
    chooseSize(view);
    if (!billboards && view.lists.stripPoints.empty()) return;

    // The depth the particles fade against, a copy where they can draw: the target's own is attached
    // as they draw. One on the viewmodel layer does not fade.
    Math::AABB drawn = Math::AABB::empty();
    hold(drawn, view.lists.particlesAdditive);
    hold(drawn, view.lists.particlesAlpha);
    hold(drawn, view.lists.particlesDistortion);
    hold(drawn, view.lists.stripsAdditive, view.lists.stripPoints);
    hold(drawn, view.lists.stripsAlpha, view.lists.stripPoints);
    hold(drawn, view.lists.stripsDistortion, view.lists.stripPoints);
    m_depthCopy.blitDepthFrom(ctx.sceneHDR, pixelsOf(drawn, view.camera, m_depthCopy, 1));
    m_depthCopy.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);

    // Every strip's points, once.
    if (!view.lists.stripPoints.empty()) {
        const std::vector<StripPoint>& strips = view.lists.stripPoints;
        const auto bytes = static_cast<uint32_t>(strips.size() * sizeof(StripPoint));
        const Vkm::OpenGL::BufferSpan points = ctx.mirror.stream().write(strips.data(), bytes);
        Vkm::OpenGL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::STRIP_POINTS, points);
    }

    // Depth-tested, never depth-writing. Into the reflection inputs too: a particle hides the
    // reflection behind it as it hides the surface (shaders/opengl/scene/particle/shade.glsl).
    ctx.gl.setDepthWrite(false);
    setReflectRoughnessWritable(ctx.gl, false);

    for (Vkm::OpenGL::Shader* program : {&m_shader, &m_strip}) {
        program->bind();
        bindFog(ctx, *program);
        bindAmbient(ctx, *program);
    }

    // An alpha-blended particle is lit as it draws: the lights by its cluster, through their
    // shadows' hard lookups, which compare and never read the raw depth, and the environment
    // (shaders/opengl/scene/particle/light.glsl).
    ctx.clusters.bind();
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);

    // Additive first (its blend is commutative), then alpha far to near at half size, then what
    // bends the scene as all of them left it.
    ctx.sceneHDR.bindForwardPass(ctx.gl);
    ctx.gl.setBlending(true);
    drawBlend(ctx, ParticleBlend::Additive, view.lists.particlesAdditive, view.lists.stripsAdditive);
    drawHalf(ctx);
    if (!view.lists.particlesDistortion.empty() || !view.lists.stripsDistortion.empty()) {
        // Into the post chain's free scratch, as the forward pass's refraction does: nothing
        // reads it until the post passes draw over it. Only where a bend can read, as far round
        // the haze as the strongest bends (shaders/opengl/scene/particle/shade.glsl, writeBent).
        Math::AABB bent = Math::AABB::empty();
        // One on the viewmodel layer is drawn through another projection: all of it, then.
        const bool layeredCards  = hold(bent, view.lists.particlesDistortion);
        const bool layeredStrips = hold(bent, view.lists.stripsDistortion, view.lists.stripPoints);
        const float height = static_cast<float>(view.sceneHeight());
        const int reach = static_cast<int>(std::ceil(bendReach(view) * height)) + 1;
        const GLTarget::PixelBox read = layeredCards || layeredStrips
            ? ctx.colorDst->pixels()
            : pixelsOf(bent, view.camera, *ctx.colorDst, reach);
        ctx.colorDst->blitColorFrom(ctx.sceneHDR, read);
        ctx.colorDst->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
        ctx.sceneHDR.bindForwardPass(ctx.gl);  // the blit left the scratch bound
        const RenderView::SceneLists& lists = view.lists;
        drawBlend(ctx, ParticleBlend::Distortion, lists.particlesDistortion, lists.stripsDistortion);
    }
}

void GLParticlePass::chooseSize(const RenderView& view) {
    const float covered = view.lists.particlesAlpha.empty() ? 0.0f : screensCovered(view);
    const bool  wanted  = covered >= (m_halfSize ? FULL_RESOLUTION_COVER : HALF_RESOLUTION_COVER);
    if (wanted != m_halfSize && m_sizeHeld >= RESOLUTION_HOLD_FRAMES) {
        m_halfSize = wanted;
        m_sizeHeld = 0;
    }
    m_sizeHeld = std::min(m_sizeHeld + 1, RESOLUTION_HOLD_FRAMES);
}

void GLParticlePass::drawHalf(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.lists.particlesAlpha.empty() && view.lists.stripsAlpha.empty()) return;
    // A few puffs cost less at full size than the half-size target's own draws.
    if (!m_halfSize) {
        drawBlend(ctx, ParticleBlend::Alpha, view.lists.particlesAlpha, view.lists.stripsAlpha);
        return;
    }
    namespace Particle = GLBindings::ParticleTextureSlots;
    namespace Post     = GLBindings::PostTextureSlots;
    using Attachment   = GLTarget::Attachment;

    // The scene's depth at half size, and the color emptied, in one draw; its copy to fade on.
    const glm::uvec2 size(view.sceneWidth(), view.sceneHeight());
    m_half.bind(ctx.gl);
    ctx.gl.setBlending(false);
    ctx.gl.setDepthFunc(GL_ALWAYS);
    ctx.gl.setDepthWrite(true);
    m_halfDepth.bind();
    ctx.screenTri.draw();
    m_halfDepthCopy.blitDepthFrom(m_half, m_half.pixels());
    m_halfDepthCopy.bindTexture(Attachment::Depth, Post::SCENE_DEPTH);

    // Drawn through a camera block of that size: fog reads the screen by the viewport.
    m_halfCamera.update(view.camera, glm::vec2((size + 1u) / 2u));
    m_half.bind(ctx.gl);
    ctx.gl.setDepthFunc(GLDepth::EYE_TEST_EQUAL);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    drawBlend(ctx, ParticleBlend::Alpha, view.lists.particlesAlpha, view.lists.stripsAlpha);
    m_camera.update(view.camera, glm::vec2(size));

    // Over the scene as one more particle, and back to the full-size depth for what draws after.
    ctx.sceneHDR.bindForwardPass(ctx.gl);
    ctx.gl.setBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setDepthTest(false);
    m_depthCopy.bindTexture(Attachment::Depth, Post::SCENE_DEPTH);
    m_half.bindTexture(Attachment::Color, Particle::HALF_COLOR);
    m_halfDepthCopy.bindTexture(Attachment::Depth, Particle::HALF_DEPTH);
    m_composite.bind();
    ctx.screenTri.draw();
    ctx.gl.setDepthTest(true);
}

} // namespace Vkm::Engine
