#include "pass/gl_planar_reflection_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "gl_mip_levels.h"
#include "gl_view.h"
#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "frame/gl_camera.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "offline/gl_scene_capture.h"
#include "pass/gl_particle_pass.h"
#include "pass/gl_reflection_pass.h"
#include "storage/gl_ibl.h"
#include "storage/gl_ocean_fields.h"
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

/// How far @p box reaches in front of @p plane: its farthest corner's signed distance.
float reachInFront(const Math::AABB& box, const glm::vec4& plane) {
    const glm::vec3 n(plane);
    return glm::dot(n, box.center()) + plane.w + glm::dot(glm::abs(n), box.halfExtent());
}

} // namespace

GLPlanarReflectionPass::GLPlanarReflectionPass(
    GLSceneCapture& capture,
    GLCamera& camera,
    GLParticlePass& particles
)
    : m_capture(capture)
    , m_camera(camera)
    , m_particles(particles)
    , m_skinned("shaders/forward/pbr_skinned")
    , m_source("shaders/reflection/source")
    , m_downsample("shaders/reflection/downsample") {
    for (auto& target : m_targets) target = std::make_unique<GLTarget>(GLTarget::Layout::ColorDepth);
}

GLPlanarReflectionPass::~GLPlanarReflectionPass() = default;

void GLPlanarReflectionPass::prepare(const RenderView& view, const GLView& glView) {
    m_count = 0;
    if (!view.hasCamera || view.planarReflectors.empty()) return;

    // The ones the camera sees from their front, the most of the screen first.
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
        // Its area as the eye sees it: facing, over the distance squared.
        const glm::vec3 toward = r.center - eye;
        const float     dist2  = std::max(glm::dot(toward, toward), 1e-4f);
        const float     cover  = 4.0f * r.halfExtents.x * r.halfExtents.y * front / std::sqrt(dist2) / dist2;
        for (int slot = 0; slot < MAX_DRAWN; ++slot) {
            if (best[slot].second >= 0 && cover <= best[slot].first) continue;
            for (int k = MAX_DRAWN - 1; k > slot; --k) best[k] = best[k - 1];
            best[slot] = {cover, i};
            break;
        }
    }

    const std::vector<Math::AABB>& bounds = view.objects->bounds;
    const std::vector<ObjectDraw>& draws  = view.objects->draws;
    for (const auto& [cover, index] : best) {
        if (index < 0) continue;
        Drawn& drawn = m_drawn[m_count++];
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

        // What that camera sees in front of the plane.
        const Math::Frustum mirrored = Math::extractFrustum(drawn.camera.viewProjection);
        drawn.objects.clear();
        drawn.glass.clear();
        for (const uint32_t object : view.objects->scene) {
            if (draws[object].viewmodel) continue;
            if (reachInFront(bounds[object], drawn.plane) <= CLIP_OFFSET) continue;
            if (!Math::frustumIntersectsAABB(mirrored, bounds[object])) continue;
            const GLMaterial* material = glView.getMaterial(draws[object].material);
            if (!material) continue;
            const bool glass = material->getType() == MaterialType::Transparent;
            (glass ? drawn.glass : drawn.objects).push_back(object);
        }
    }
}

void GLPlanarReflectionPass::execute(GLFrameContext& ctx) {
    if (m_count == 0) return;
    for (int i = 0; i < m_count; ++i) {
        draw(ctx, i);
        m_pictures[i] = {&m_drawn[i].reflector, &m_chains[i]};
    }
    ctx.planarPictures = m_pictures.data();
    ctx.planarCount    = m_count;

    // The frame's camera back, for every pass after.
    const RenderView& view = ctx.view;
    m_camera.update(view.camera, glm::vec2(view.sceneWidth(), view.sceneHeight()));
    VKM_GL_CHECK(glFrontFace(GL_CCW));
    VKM_GL_CHECK(glDisable(GL_CLIP_DISTANCE0));
    ctx.gl.setFaceCulling(false);
}

void GLPlanarReflectionPass::draw(GLFrameContext& ctx, int i) {
    const RenderView& view   = ctx.view;
    const GLView&     glView = ctx.resources;
    Drawn&            drawn  = m_drawn[i];
    GLTarget&         target = *m_targets[i];

    const uint32_t width  = drawn.size.x;
    const uint32_t height = drawn.size.y;
    target.resize(width, height, 1, false);
    target.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GLDepth::EYE_TEST);
    ctx.gl.setBlending(false);
    ctx.gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    ctx.gl.clear(true, true, false, GLDepth::EYE_CLEAR);
    m_camera.update(drawn.camera, glm::vec2(width, height));

    // The sky behind it all, where depth is still clear.
    if (ctx.ibl.isReady()) {
        m_capture.drawSky(ctx.gl, ctx.ibl, drawn.camera.projection, view.environment.sky.intensity);
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
    const bool posed = ctx.skinPalette.count() > 0;
    if (posed) ctx.skinPalette.bind();

    // Clipped where the plane is: what lies behind it is not in the mirror.
    const glm::vec4 clip(glm::vec3(drawn.plane), drawn.plane.w - CLIP_OFFSET);
    Vkm::GL::Shader& pbr = m_capture.pbrProgram();
    for (Vkm::GL::Shader* program : {&pbr, &m_skinned}) {
        if (program == &m_skinned && !posed) continue;
        program->bind();
        bindAmbient(ctx, *program);
        program->setUniform1i("u_hasFog", 0);
        program->setUniform1i("u_hasAir", 0);
        program->setUniform1i("u_hasAO", 0);
        program->setUniform1i("u_hasScreenShadow", 0);
        program->setUniform1i("u_useClusters", 0);
        program->setUniform1i("u_renderMode", 0);
        program->setUniform1i("u_wirePass", 0);
        program->setUniform1i("u_hasSceneColor", 0);
        program->setUniform1i("u_alphaToCoverage", 0);
        program->setUniform4fv("u_oceanShore", ctx.oceanFields.shore());
        program->setUniform4fv("u_clipPlane", clip);
    }
    VKM_GL_CHECK(glEnable(GL_CLIP_DISTANCE0));
    // A mirror image turns every triangle's winding.
    VKM_GL_CHECK(glFrontFace(GL_CW));
    ctx.gl.setCullFace(GL_BACK);

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const uint32_t bones = ctx.skinPalette.count();
    for (const InstanceDraw& run : m_batcher.buildGrouped(drawn.objects, *view.objects, glView, bones)) {
        Vkm::GL::Shader& program = run.skinned ? m_skinned : pbr;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        const GLMaterial* material = glView.getMaterial(run.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
            boundMaterial = material;
            ctx.gl.setFaceCulling(!material->doubleSided());
        }
        m_batcher.draw(ctx.gl, run);
    }
    if (!drawn.glass.empty()) drawGlass(ctx, i);
    m_particles.drawMirrored(ctx, target, drawn.size, drawn.camera, clip);

    // Unclipped again for whoever draws with these programs next.
    for (Vkm::GL::Shader* program : {&pbr, &m_skinned}) {
        if (program == &m_skinned && !posed) continue;
        program->setUniform4fv("u_clipPlane", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    VKM_GL_CHECK(glDisable(GL_CLIP_DISTANCE0));
    VKM_GL_CHECK(glFrontFace(GL_CCW));

    // Filtered down, as the reflection pass filters the scene, so a rough surface reads it blurred.
    Vkm::GL::MipChainTexture& chain = m_chains[i];
    if (!chain.isReady() || chain.mipWidth(0) != static_cast<int>(width)
        || chain.mipHeight(0) != static_cast<int>(height)) {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        const int levels = mipLevelsDownToTwo(w, h, GLReflectionPass::MAX_CHAIN_MIPS);
        chain.create(w, h, levels, GL_RGBA16F, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
    }
    constexpr uint32_t TARGET_UNIT = 0;
    const auto dispatchLevel = [&](const Vkm::GL::ComputeShader& shader, int mip) {
        chain.bindImage(mip, TARGET_UNIT, GL_WRITE_ONLY);
        namespace Groups = GLBindings::ComputeGroups;
        shader.dispatch(
            Groups::covering(static_cast<uint32_t>(chain.mipWidth(mip)),  Groups::IMAGE),
            Groups::covering(static_cast<uint32_t>(chain.mipHeight(mip)), Groups::IMAGE)
        );
        VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
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
    GLTarget&         target = *m_targets[i];

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
    m_backdrop.resize(drawn.size.x, drawn.size.y, 1, false);
    m_backdrop.blitColorFrom(target);
    target.bind(ctx.gl);
    m_backdrop.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);

    Vkm::GL::Shader& pbr   = m_capture.pbrProgram();
    const bool       posed = ctx.skinPalette.count() > 0;
    for (Vkm::GL::Shader* program : {&pbr, &m_skinned}) {
        if (program == &m_skinned && !posed) continue;
        program->bind();
        program->setUniform1i("u_hasSceneColor", 1);
    }
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setFaceCulling(true);

    const GLMaterial*      boundMaterial = nullptr;
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const uint32_t bones = ctx.skinPalette.count();
    for (const InstanceDraw& run : m_batcher.buildSequential(drawn.glass, *view.objects, glView, bones)) {
        Vkm::GL::Shader& program = run.skinned ? m_skinned : pbr;
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        const GLMaterial* material = glView.getMaterial(run.material);
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

    for (Vkm::GL::Shader* program : {&pbr, &m_skinned}) {
        if (program == &m_skinned && !posed) continue;
        program->bind();
        program->setUniform1i("u_hasSceneColor", 0);
    }
    ctx.gl.setBlending(false);
    ctx.gl.setDepthWrite(true);
}

} // namespace Vkm::Engine
