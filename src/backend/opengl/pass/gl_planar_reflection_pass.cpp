#include "pass/gl_planar_reflection_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_context.h"

#include "gl_forward_programs.h"
#include "gl_frame_context.h"
#include "gl_mip_levels.h"
#include "gl_view.h"
#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "convention/gl_sky_projection.h"
#include "frame/gl_camera.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "offline/gl_scene_capture.h"
#include "pass/gl_cluster_pass.h"
#include "pass/gl_particle_pass.h"
#include "pass/gl_reflection_pass.h"
#include "storage/gl_ibl.h"
#include "storage/gl_lightmaps.h"
#include "storage/gl_ocean_fields.h"
#include "storage/gl_ocean_shore.h"
#include "storage/gl_shadow_atlas.h"
#include "core/math/frustum.h"
#include "debug/profiler.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// Metres in front of the plane the picture starts: the reflecting surface itself is clipped.
constexpr float CLIP_OFFSET = 0.01f;

/// The mirror image in the plane @p plane (unit normal, then its distance), as a matrix.
glm::mat4 mirrorIn(const glm::vec4& plane) {
    const glm::vec3 n(plane);
    glm::mat4 m(1.0f);
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) m[c][r] -= 2.0f * n[r] * n[c];
    }
    m[3] = glm::vec4(-2.0f * plane.w * n, 1.0f);
    return m;
}

/// Of the screen's width and height, the margin round a reflector's rectangle that its picture is drawn
/// over too: as far as the resolve's bend reads off it.
constexpr float PICTURE_MARGIN = GLPlanarReflectionPass::PICTURE_BEND;

/// The share of the screen below which a reflector is left to the trace and the probes: a picture a few
/// pixels across is not worth a second drawing of the scene.
constexpr float MIN_SCREEN_SHARE = 0.0005f;

/// Where reflector @p r lies on the screen @p viewProjection draws: x0, y0, x1, y1 in [0, 1]. The whole
/// screen while a corner is behind the eye.
glm::vec4 screenRectOf(const PlanarReflectorData& r, const glm::mat4& viewProjection) {
    glm::vec2 lo(1.0f);
    glm::vec2 hi(0.0f);
    for (const float u : {-1.0f, 1.0f}) {
        for (const float v : {-1.0f, 1.0f}) {
            const glm::vec3 along  = r.axisU * (u * r.halfExtents.x) + r.axisV * (v * r.halfExtents.y);
            const glm::vec4 clip   = viewProjection * glm::vec4(r.center + along, 1.0f);
            if (clip.w <= 1e-4f) return {0.0f, 0.0f, 1.0f, 1.0f};
            const glm::vec2 at = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
            lo = glm::min(lo, at);
            hi = glm::max(hi, at);
        }
    }
    return {glm::clamp(lo, 0.0f, 1.0f), glm::clamp(hi, 0.0f, 1.0f)};
}

/// @p projection narrowed to @p rect of its screen, for culling to what that part of the screen shows.
glm::mat4 narrowedTo(const glm::mat4& projection, const glm::vec4& rect) {
    const glm::vec2 lo   = glm::vec2(rect.x, rect.y) * 2.0f - 1.0f;
    const glm::vec2 hi   = glm::vec2(rect.z, rect.w) * 2.0f - 1.0f;
    const glm::vec2 span = glm::max(hi - lo, glm::vec2(1e-4f));
    glm::mat4 crop(1.0f);
    crop[0][0] = 2.0f / span.x;
    crop[1][1] = 2.0f / span.y;
    crop[3][0] = -(hi.x + lo.x) / span.x;
    crop[3][1] = -(hi.y + lo.y) / span.y;
    return crop * projection;
}

/// How far @p box lies from @p point: 0 when it holds it.
float distanceTo(const Math::AABB& box, const glm::vec3& point) {
    return glm::distance(glm::clamp(point, box.min, box.max), point);
}

/// How far @p box reaches in front of @p plane: its farthest corner's signed distance.
float reachInFront(const Math::AABB& box, const glm::vec4& plane) {
    const glm::vec3 n(plane);
    return glm::dot(n, box.center()) + plane.w + glm::dot(glm::abs(n), box.halfExtent());
}

} // namespace

GLPlanarReflectionPass::GLPlanarReflectionPass(
    GLSceneCapture& capture,
    GLForwardPrograms& programs,
    GLCamera& camera,
    GLParticlePass& particles,
    GLClusterPass& lightCull
)
    : m_capture(capture)
    , m_programs(programs)
    , m_camera(camera)
    , m_particles(particles)
    , m_lightCull(lightCull)
    , m_source("shaders/reflection/source")
    , m_downsample("shaders/reflection/downsample") {
    for (Slot& slot : m_slots) {
        slot.target   = std::make_unique<GLTarget>(GLTarget::Layout::ColorDepth);
        slot.backdrop = std::make_unique<GLTarget>(GLTarget::Layout::Color);
        slot.depth    = std::make_unique<GLTarget>(GLTarget::Layout::ColorDepth, GL_R8);
    }
}

GLPlanarReflectionPass::~GLPlanarReflectionPass() = default;

void GLPlanarReflectionPass::prepare(const RenderView& view, const GLView& glView) {
    m_count = 0;
    if (!view.hasCamera || view.planarReflectors.empty()) return;

    // The ones the camera sees from their front, the most of the screen first; one too small to
    // matter not at all (MIN_SCREEN_SHARE).
    const glm::vec3     eye   = view.camera.position;
    const Math::Frustum seen  = Math::extractFrustum(view.camera.viewProjection);
    std::array<std::pair<float, int>, MAX_DRAWN> best{};
    best.fill({0.0f, -1});
    for (int i = 0; i < static_cast<int>(view.planarReflectors.size()); ++i) {
        const PlanarReflectorData& r = view.planarReflectors[i];
        const float front = glm::dot(eye - r.center, r.normal);
        if (front <= 0.01f) continue;
        const glm::vec3 reach = glm::abs(r.axisU) * r.halfExtents.x + glm::abs(r.axisV) * r.halfExtents.y;
        if (!Math::frustumIntersectsAABB(seen, Math::AABB{r.center - reach, r.center + reach})) continue;
        const glm::vec4 rect  = screenRectOf(r, view.camera.viewProjection);
        const float     cover = (rect.z - rect.x) * (rect.w - rect.y);
        if (cover < MIN_SCREEN_SHARE) continue;
        for (int slot = 0; slot < MAX_DRAWN; ++slot) {
            if (best[slot].second >= 0 && cover <= best[slot].first) continue;
            for (int k = MAX_DRAWN - 1; k > slot; --k) best[k] = best[k - 1];
            best[slot] = {cover, i};
            break;
        }
    }

    // Each into the slot it drew into last, the rest into those left.
    std::array<int, MAX_DRAWN> slotFor{};
    slotFor.fill(-1);
    std::array<bool, MAX_DRAWN> taken{};
    const auto place = [&](bool itsOwn) {
        for (int k = 0; k < MAX_DRAWN; ++k) {
            if (best[k].second < 0 || slotFor[k] >= 0) continue;
            for (int s = 0; s < MAX_DRAWN; ++s) {
                if (taken[s] || (itsOwn && m_slots[s].reflector != best[k].second)) continue;
                slotFor[k] = s;
                taken[s]   = true;
                break;
            }
        }
    };
    place(true);
    place(false);

    const std::vector<Math::AABB>& bounds = view.objects->bounds;
    const std::vector<ObjectDraw>& draws  = view.objects->draws;
    for (int k = 0; k < MAX_DRAWN; ++k) {
        const int index = best[k].second;
        if (index < 0) continue;
        Drawn& drawn = m_drawn[m_count++];
        drawn.slot = slotFor[k];
        m_slots[drawn.slot].reflector = index;
        const PlanarReflectorData& r = view.planarReflectors[index];
        drawn.reflector = r;
        drawn.plane     = glm::vec4(r.normal, -glm::dot(r.normal, r.center));

        // The camera mirrored in the plane, seeing what the plane reflects to the eye.
        const glm::mat4 mirror = mirrorIn(drawn.plane);
        drawn.camera = CameraData::from(
            view.camera.view * mirror,
            view.camera.projection,
            glm::vec3(mirror * glm::vec4(eye, 1.0f))
        );
        drawn.size = glm::max(
            glm::uvec2(glm::vec2(view.sceneWidth(), view.sceneHeight()) * r.renderScale),
            glm::uvec2(16u)
        );

        // Its picture is read where the eye sees the reflector, so only that part of the screen is
        // drawn, and only what that part shows of the scene.
        const glm::vec4 margin(-PICTURE_MARGIN, -PICTURE_MARGIN, PICTURE_MARGIN, PICTURE_MARGIN);
        drawn.rect = glm::clamp(screenRectOf(r, view.camera.viewProjection) + margin, 0.0f, 1.0f);

        // What that camera sees in front of the plane.
        const glm::mat4     narrowed = narrowedTo(drawn.camera.projection, drawn.rect);
        const Math::Frustum mirrored = Math::extractFrustum(narrowed * drawn.camera.view);
        drawn.objects.clear();
        drawn.glass.clear();
        for (const uint32_t object : view.objects->scene) {
            if (draws[object].viewmodel) continue;
            if (reachInFront(bounds[object], drawn.plane) <= CLIP_OFFSET) continue;
            if (r.drawDistance > 0.0f && distanceTo(bounds[object], r.center) > r.drawDistance) continue;
            if (!Math::frustumIntersectsAABB(mirrored, bounds[object])) continue;
            const GLMaterial* material = glView.getMaterial(draws[object].material);
            if (!material) continue;
            const bool glass = material->getType() == MaterialType::Transparent;
            (glass ? drawn.glass : drawn.objects).push_back(object);
        }
    }
}

int GLPlanarReflectionPass::slotOf(int reflector) const {
    for (int s = 0; s < MAX_DRAWN; ++s) {
        if (m_slots[s].reflector == reflector) return s;
    }
    return -1;
}

void GLPlanarReflectionPass::execute(GLFrameContext& ctx) {
    if (m_count == 0) return;
    for (int i = 0; i < m_count; ++i) {
        draw(ctx, i);
        m_pictures[i] = {&m_drawn[i].reflector, &m_slots[m_drawn[i].slot].chain, m_drawn[i].rect};
    }
    ctx.planarPictures = m_pictures.data();
    ctx.planarCount    = m_count;

    // The frame's camera and light lists back, for every pass after.
    const RenderView& view = ctx.view;
    ctx.clusters.bind();
    m_camera.update(view.camera, glm::vec2(view.sceneWidth(), view.sceneHeight()));
}

void GLPlanarReflectionPass::draw(GLFrameContext& ctx, int i) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;
    Drawn&            drawn  = m_drawn[i];
    Slot&             slot   = m_slots[drawn.slot];
    GLTarget&         target = *slot.target;

    const uint32_t width  = drawn.size.x;
    const uint32_t height = drawn.size.y;
    target.resize(ctx.gl, width, height, 1, false);
    target.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GLDepth::EYE_TEST);
    ctx.gl.setBlending(false);
    ctx.gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    ctx.gl.clear(true, true, false, GLDepth::EYE_CLEAR);
    m_camera.update(drawn.camera, glm::vec2(width, height));

    // Its lights culled for its camera as the camera's are, into lists of its own.
    slot.clusters.init();
    m_lightCull.cull(slot.clusters);

    const glm::ivec2 from(glm::floor(glm::vec2(drawn.rect.x, drawn.rect.y) * glm::vec2(drawn.size)));
    const glm::ivec2 to(glm::ceil(glm::vec2(drawn.rect.z, drawn.rect.w) * glm::vec2(drawn.size)));
    ctx.gl.enableScissor(true);
    ctx.gl.setScissor(from.x, from.y, to.x - from.x, to.y - from.y);

    // The sky behind it all, where depth is still clear, as the frame shows one; none past a draw
    // distance, a closed room's.
    const bool sky = ctx.ibl.isReady() && view.environment.sky.showSkybox;
    if (sky && drawn.reflector.drawDistance <= 0.0f) {
        m_capture.drawSky(ctx.gl, ctx.ibl, skyProjection(drawn.camera), view.environment.sky.intensity);
    }

    // Lit as the forward pass lights, less what is the camera's screen.
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);
    ctx.shadowAtlas.bindCubesRaw(GLBindings::ShadowTextureSlots::CUBE_RAW);
    if (ctx.ibl.isReady()) ctx.ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    ctx.ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);
    ctx.objects.bind();
    ctx.oceanFields.bind();
    ctx.oceanShore.bind();
    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();
    // What a fixed light baked, read as the frame reads it: a point behind the camera has no
    // cascade, and would show lit.
    const bool baked = ctx.lightmaps && ctx.lightmaps->any();
    if (baked) ctx.lightmaps->bindPages();

    // Clipped where the plane is: what lies behind it is not in the mirror.
    const glm::vec4 clip(glm::vec3(drawn.plane), drawn.plane.w - CLIP_OFFSET);
    m_programs.forEach(posed, true, [&](Vkm::GL::Shader& shown) {
        Vkm::GL::Shader* program = &shown;
        if (program->hasUniform("u_hasLightmaps")) program->setUniform1i("u_hasLightmaps", baked ? 1 : 0);
        program->bind();
        bindAmbient(ctx, *program);
        program->setUniform1i("u_hasFog", 0);
        program->setUniform1i("u_hasAir", 0);
        program->setUniform1i("u_hasAO", 0);
        program->setUniform1i("u_hasScreenShadow", 0);
        program->setUniform1i("u_useClusters", 1);
        program->setUniform1i("u_hardShadows", 1);
        program->setUniform1i("u_renderMode", 0);
        program->setUniform1i("u_wirePass", 0);
        // The plain and layered programs are built without transmission, so are told of no backdrop.
        if (program->hasUniform("u_hasSceneColor")) program->setUniform1i("u_hasSceneColor", 0);
        program->setUniform1i("u_alphaToCoverage", 0);
        program->setUniform4fv("u_oceanShore", ctx.oceanShore.area());
        program->setUniform4fv("u_clipPlane", clip);
    });
    ctx.gl.setClipDistance(0, true);
    // A mirror image turns every triangle's winding.
    ctx.gl.setFrontFace(GL_CW);
    ctx.gl.setCullFace(GL_BACK);

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const uint32_t bones = ctx.skinPalette.count();
    for (const InstanceDraw& run : m_batcher.buildGrouped(drawn.objects, *view.objects, glView, bones)) {
        // A cut-out is never baked into, so reads no lightmap.
        const GLMaterial* material = glView.getMaterial(run.material);
        const bool        opaque   = material && material->getType() == MaterialType::Opaque;
        Vkm::GL::Shader&  program  = m_programs.programFor(material, run.skinned, opaque);
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
            ctx.gl.setFaceCulling(!material->doubleSided());
        }
        m_batcher.draw(ctx.gl, run);
    }
    if (!drawn.glass.empty()) drawGlass(ctx, i);
    m_particles.drawMirrored(ctx, target, *slot.depth, drawn.size, drawn.camera, clip);

    // Unclipped again for whoever draws with these programs next.
    m_programs.forEach(posed, true, [](Vkm::GL::Shader& program) {
        program.setUniform4fv("u_clipPlane", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    });
    ctx.gl.setClipDistance(0, false);
    ctx.gl.setFrontFace(GL_CCW);
    ctx.gl.enableScissor(false);

    // Filtered down, as the reflection pass filters the scene, so a rough surface reads it blurred.
    Vkm::GL::MipChainTexture& chain = slot.chain;
    if (!chain.isReady() || chain.mipWidth(0) != static_cast<int>(width)
        || chain.mipHeight(0) != static_cast<int>(height)) {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        const int levels = mipLevelsDownToTwo(w, h, GLReflectionPass::MAX_CHAIN_MIPS);
        chain.create(w, h, levels, GL_RGBA16F, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
    }
    constexpr uint32_t TARGET_UNIT = 0;
    namespace Groups = GLBindings::ComputeGroups;
    const auto dispatchLevel = [&](const Vkm::GL::ComputeShader& shader, int mip) {
        chain.bindImage(mip, TARGET_UNIT, GL_WRITE_ONLY);
        shader.dispatch(
            Groups::covering(static_cast<uint32_t>(chain.mipWidth(mip)),  Groups::IMAGE),
            Groups::covering(static_cast<uint32_t>(chain.mipHeight(mip)), Groups::IMAGE)
        );
        Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    };
    m_source.bind();
    target.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
    dispatchLevel(m_source, 0);
    m_downsample.bind();
    for (int mip = 1; mip < chain.mipCount(); ++mip) {
        m_downsample.setUniform1i("u_first", mip == 1 ? 1 : 0);
        chain.bindLevel(mip - 1, GLBindings::ReflectionTextureSlots::CHAIN);
        dispatchLevel(m_downsample, mip);
    }
}

void GLPlanarReflectionPass::drawGlass(GLFrameContext& ctx, int i) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;
    Drawn&            drawn  = m_drawn[i];
    GLTarget&         target = *m_slots[drawn.slot].target;
    GLTarget&         behind = *m_slots[drawn.slot].backdrop;

    // Far to near from the mirror's eye, so the blend composes.
    const std::vector<glm::mat4>& models = view.objects->models;
    const glm::vec3 eye = drawn.camera.position;
    const auto away = [&](uint32_t object) {
        const glm::vec3 to = eye - glm::vec3(models[object][3]);
        return glm::dot(to, to);
    };
    std::sort(drawn.glass.begin(), drawn.glass.end(), [&](uint32_t a, uint32_t b) {
        return away(a) > away(b);
    });

    // What is behind the glass, to refract: the picture so far.
    behind.resize(ctx.gl, drawn.size.x, drawn.size.y, 1, false);
    behind.blitColorFrom(target);
    target.bind(ctx.gl);
    behind.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);

    const bool posed = ctx.skinPalette.count() > 0;
    const auto backdrop = [&](int shown) {
        m_programs.forEach(posed, false, [&](Vkm::GL::Shader& program) {
            if (program.hasUniform("u_hasSceneColor")) program.setUniform1i("u_hasSceneColor", shown);
        });
    };
    backdrop(1);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setFaceCulling(true);

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const uint32_t bones = ctx.skinPalette.count();
    for (const InstanceDraw& run : m_batcher.buildSequential(drawn.glass, *view.objects, glView, bones)) {
        const GLMaterial* material = glView.getMaterial(run.material);
        Vkm::GL::Shader&  program  = m_programs.programFor(material, run.skinned, false);
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
        }
        // Blending does not sort a mesh's own faces: the far side first, then the near.
        if (material && material->doubleSided()) {
            ctx.gl.setCullFace(GL_FRONT);
            m_batcher.draw(ctx.gl, run);
            ctx.gl.setCullFace(GL_BACK);
        }
        m_batcher.draw(ctx.gl, run);
    }

    backdrop(0);
    ctx.gl.setBlending(false);
    ctx.gl.setDepthWrite(true);
}

} // namespace Vkm::Engine
