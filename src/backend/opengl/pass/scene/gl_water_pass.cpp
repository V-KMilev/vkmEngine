#include "pass/scene/gl_water_pass.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"

#include "pass/gl_frame_context.h"
#include "gl_profiler.h"
#include "frame/water/gl_waves.h"
#include "storage/water/gl_wave_fields.h"
#include "storage/water/gl_water_shore.h"
#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "core/frame/render_objects.h"
#include "ecs/water/wave_frame.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Groups = GLBindings::ComputeGroups;

// The shore's square: meters on an edge, texels across it, and how far ahead of the eye its
// middle lies, in edges - most of the sea the eye sees is ahead of it.
constexpr float SHORE_EDGE  = 128.0f;
constexpr int   SHORE_SIZE  = 512;
constexpr float SHORE_AHEAD = 0.3f;
// Texels across the water's height over the square: a meter, filtered, against waves of a few
// meters and more (WaveShape::minWavelength), whose shortest stand a few centimeters high.
constexpr int   SHORE_WATER = 128;
// How far round a point the distance transform looks, in waterline widths, at most SHORE_MOST
// texels: the foam keeps inside it, whatever the waves and suds ask (water.glsl,
// waterlineFoam).
constexpr float SHORE_REACH = 2.0f;
constexpr int   SHORE_MOST  = 32;
// What the shore holds where nothing lies under the water: far past any reach.
constexpr float SHORE_NOTHING = 10000.0f;
// Meters under the water's lowest the shore still draws: deeper counts past any reach.
constexpr float SHORE_DEPTH = 2.0f;

// Whether @p next can go out in one multi-draw with @p draw, for a pass that binds no material: the
// same program, winding and vertex layout, its commands just after.
bool joins(const InstanceDraw& draw, const InstanceDraw& next) {
    return next.skinned == draw.skinned && next.mirrored == draw.mirrored
        && next.mesh->layout() == draw.mesh->layout() && next.first == draw.first + draw.count;
}

} // namespace

GLWaterPass::GLWaterPass()
    : m_draw("shaders/opengl/scene/water/shore")
    , m_drawSkinned("shaders/opengl/scene/water/shore", {"SKINNED"})
    , m_water("shaders/opengl/scene/water/shore/water")
    , m_rows("shaders/opengl/scene/water/shore/rows")
    , m_columns("shaders/opengl/scene/water/shore/columns") {}

GLWaterPass::~GLWaterPass() = default;

void GLWaterPass::execute(GLFrameContext& ctx) {
    if (GLWaves::rippling(ctx.view.lists.waves, ctx.waterShown)) {
        PROFILE_GPU_SCOPE("Water/Ripples");
        ctx.waveFields.drawRipples(ctx.view.frameSeconds);
    } else {
        ctx.waveFields.forgetRipples();
    }
    if (!ctx.waterShown || !ctx.view.hasCamera || ctx.shore.waterline <= 0.0f) {
        ctx.waterShore.forget();
        return;
    }
    PROFILE_GPU_SCOPE("Water/Shore");
    captureShore(ctx);
}

void GLWaterPass::captureShore(GLFrameContext& ctx) {
    m_owed += ctx.view.frameSeconds * CAPTURE_RATE;
    if (ctx.waterShore.held() && m_owed < 1.0f && ctx.view.frameSeconds > 0.0f) return;
    m_owed = 0.0f;

    const WaveFrame&  waves = ctx.view.lists.waves;
    const WaterShore& water = ctx.shore;
    if (!m_half) {
        const auto texture = [](const char* name, GLenum format, uint32_t size = SHORE_SIZE) {
            const Vkm::OpenGL::Texture2DParams params = Vkm::OpenGL::renderTargetParams(
                size,
                size,
                format,
                GL_RED,
                Vkm::OpenGL::TextureMinFilter::Linear,
                Vkm::OpenGL::TextureMagFilter::Linear
            );
            return std::make_unique<Vkm::OpenGL::Texture2D>(name, params);
        };
        m_height     = texture("water_shore_height", GL_R16F, SHORE_WATER);
        m_under      = texture("water_shore_under", GL_R16F);
        m_underDepth = texture("water_shore_depth", GL_DEPTH_COMPONENT24);
        m_over       = texture("water_shore_over", GL_R16F);
        m_overDepth  = texture("water_shore_over_depth", GL_DEPTH_COMPONENT24);
        m_half       = texture("water_shore_half", GL_R32F);
        ctx.waterShore.allocate(SHORE_SIZE);
        m_underTarget.attachTexture(GL_COLOR_ATTACHMENT0, m_under->getID());
        m_underTarget.attachTexture(GL_DEPTH_ATTACHMENT, m_underDepth->getID());
        m_underTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
        m_overTarget.attachTexture(GL_COLOR_ATTACHMENT0, m_over->getID());
        m_overTarget.attachTexture(GL_DEPTH_ATTACHMENT, m_overDepth->getID());
        m_overTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    // The square, ahead of the eye on the XZ plane, snapped to whole texels so it never swims.
    const float texel = SHORE_EDGE / static_cast<float>(SHORE_SIZE);
    const glm::vec3 eye     = ctx.view.camera.position;
    const glm::vec3 forward = -glm::vec3(ctx.view.camera.invView[2]);
    glm::vec2 ahead = glm::vec2(forward.x, forward.z);
    if (glm::length(ahead) > 1e-3f) ahead = glm::normalize(ahead);
    const glm::vec2 aimed  = glm::vec2(eye.x, eye.z) + SHORE_AHEAD * SHORE_EDGE * ahead;
    const glm::vec2 middle = glm::round(aimed / texel) * texel;
    const float half = 0.5f * SHORE_EDGE;

    // Between the highest the water reaches and SHORE_DEPTH under its lowest.
    const float reach  = water.waves * waves.surface.farthestReach().y;
    const float top    = water.level + reach;
    const float bottom = water.level - reach - SHORE_DEPTH;

    // What lies over the square but water and the cut-outs: a cut-out's card is mostly air, which
    // the shore, reading no texture, would take for solid.
    const RenderObjects& objects = *ctx.view.objects;
    m_objects.clear();
    for (const uint32_t object : objects.scene) {
        const Math::AABB& box = objects.bounds[object];
        if (box.max.x < middle.x - half || box.min.x > middle.x + half) continue;
        if (box.max.z < middle.y - half || box.min.z > middle.y + half) continue;
        if (box.max.y < bottom || box.min.y > top) continue;
        if (objects.draws[object].water != 0) continue;
        const GLMaterial* material = ctx.mirror.getMaterial(objects.draws[object].material);
        if (material && material->getType() == MaterialType::AlphaMask) continue;
        m_objects.push_back(object);
    }

    // Looking straight down from the top, screen up toward -Z: row 0 is the square's most Z.
    const glm::vec3 above   = glm::vec3(middle.x, top, middle.y);
    const glm::vec3 down    = glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::mat4 view    = glm::lookAt(above, above + down, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::mat4 shoreVP = glm::ortho(-half, half, -half, half, 0.0f, top - bottom) * view;
    const glm::vec4 area    = glm::vec4(middle.x - half, middle.y + half, SHORE_EDGE, 1.0f);

    // The water's height over the square, which the draw reads in place of the sum.
    const uint32_t waterGroups = Groups::covering(static_cast<uint32_t>(SHORE_WATER), Groups::IMAGE);
    m_water.bind();
    m_water.setUniform4fv("u_area", area);
    m_water.setUniform1f("u_share", water.waves);
    m_height->bindImage(0, GL_WRITE_ONLY);
    m_water.dispatch(waterGroups, waterGroups);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    const bool drawn = !m_objects.empty();
    if (drawn) {
        const bool posed = ctx.skinPalette.count() > 0;
        m_batcher.buildGrouped(m_objects, objects, ctx.mirror, ctx.skinPalette.count());
        ctx.objects.bind();
        if (posed) ctx.skinPalette.bind();
        m_height->bindSlot(GLBindings::WaterTextureSlots::SHORE_WATER);
    }

    // Under the water the topmost thing, over it the lowest: the depth looks down in the shadow
    // convention, so the one keeps the least depth, as a shadow map does, and the other the most.
    // Each is cleared while it is bound to draw.
    const glm::vec4 clearColor = ctx.gl.getClearColor();
    ctx.gl.setClearColor(glm::vec4(SHORE_NOTHING, 0.0f, 0.0f, 0.0f));
    ctx.gl.setViewport(0, 0, SHORE_SIZE, SHORE_SIZE);
    for (const bool over : {false, true}) {
        (over ? m_overTarget : m_underTarget).bind();
        ctx.gl.clear(true, true, over ? GLDepth::FARTHEST_CLEAR : GLDepth::SHADOW_CLEAR);
        if (!drawn) continue;
        ctx.gl.setDepthFunc(over ? GLDepth::FARTHEST_TEST : GLDepth::SHADOW_TEST);
        const Vkm::OpenGL::Shader* bound = nullptr;
        const std::vector<InstanceDraw>& draws = m_batcher.draws();
        for (size_t d = 0; d < draws.size(); ++d) {
            // Runs of any material, as the shore reads none.
            InstanceDraw draw = draws[d];
            while (d + 1 < draws.size() && joins(draw, draws[d + 1])) draw.count += draws[++d].count;
            Vkm::OpenGL::Shader& program = draw.skinned ? m_drawSkinned : m_draw;
            if (&program != bound) {
                program.bind();
                program.setUniformMatrix4fv("u_shoreVP", GLDepth::shadowProjection(shoreVP));
                program.setUniform1f("u_level", water.level);
                program.setUniform4fv("u_area", area);
                program.setUniform1i("u_drawOver", over ? 1 : 0);
                program.setUniform1f("u_nothing", SHORE_NOTHING);
                bound = &program;
            }
            m_batcher.draw(ctx.gl, draw);
        }
    }
    ctx.gl.setClearColor(clearColor);

    // The distances, a row then a column at a time.
    const int wanted      = static_cast<int>(std::ceil(SHORE_REACH * water.waterline / texel));
    const int reachTexels = std::clamp(wanted, 1, SHORE_MOST);
    const uint32_t groups = Groups::covering(static_cast<uint32_t>(SHORE_SIZE), Groups::IMAGE);
    m_rows.bind();
    m_rows.setUniform1f("u_texel", texel);
    m_rows.setUniform1i("u_reach", reachTexels);
    m_under->bindSlot(GLBindings::WaterTextureSlots::SHORE_UNDER);
    m_over->bindSlot(GLBindings::WaterTextureSlots::SHORE_OVER);
    m_half->bindImage(0, GL_WRITE_ONLY);
    m_rows.dispatch(groups, groups);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    m_columns.bind();
    m_columns.setUniform1f("u_texel", texel);
    m_columns.setUniform1i("u_reach", reachTexels);
    m_half->bindSlot(GLBindings::WaterTextureSlots::SHORE_ROWS);
    ctx.waterShore.bindMapImage(0, GL_WRITE_ONLY);
    m_columns.dispatch(groups, groups);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    ctx.waterShore.hold(glm::vec4(glm::vec3(area), static_cast<float>(reachTexels) * texel));
}

} // namespace Vkm::Engine
