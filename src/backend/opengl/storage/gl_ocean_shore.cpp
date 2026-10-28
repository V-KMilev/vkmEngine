#include "storage/gl_ocean_shore.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <GL/glew.h>

#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "asset/gl_material.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "ecs/component/render/ocean.h"
#include "system/render/data/render_objects.h"
#include "system/ocean/ocean_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Groups = GLBindings::ComputeGroups;

// The shore's square: metres on an edge, texels across it, and how far ahead of the eye its
// middle lies, in edges - most of the sea the eye sees is ahead of it.
constexpr float SHORE_EDGE  = Ocean::WATERLINE_SQUARE;
constexpr int   SHORE_SIZE  = 512;
constexpr float SHORE_AHEAD = 0.3f;
// Texels across the water's height over the square: a metre, filtered, against waves of a few
// metres and more (Ocean::minWavelength), whose shortest stand a few centimetres high.
constexpr int   SHORE_WATER = 128;
// How far round a point the distance transform looks, in waterline widths, at most SHORE_MOST
// texels: the foam keeps inside it, whatever the waves and suds ask (ocean_water.glsl,
// oceanWaterline).
constexpr float SHORE_REACH = 2.0f;
constexpr int   SHORE_MOST  = 32;
// What the shore holds where nothing lies under the water: far past any reach.
constexpr float SHORE_NOTHING = 10000.0f;
// Metres under the water's lowest the shore still draws: deeper counts past any reach.
constexpr float SHORE_DEPTH = 2.0f;

// Clears @p target's color to @p color and its depth to @p depth.
void clearTarget(const Vkm::GL::FrameBuffer& target, float color, float depth) {
    const float rgba[4] = {color, 0.0f, 0.0f, 0.0f};
    target.clearColor(0, rgba);
    target.clearDepth(depth);
}

} // namespace

GLOceanShore::GLOceanShore()
    : m_draw("shaders/ocean/shore")
    , m_drawSkinned("shaders/ocean/shore_skinned")
    , m_water("shaders/ocean/shore_water")
    , m_rows("shaders/ocean/shore_rows")
    , m_columns("shaders/ocean/shore_columns") {}

GLOceanShore::~GLOceanShore() = default;

void GLOceanShore::capture(GLFrameContext& ctx) {
    const OceanData& ocean = ctx.view.ocean;
    if (!m_map) {
        const auto texture = [](const char* name, GLenum format, uint32_t size = SHORE_SIZE) {
            const Vkm::GL::Texture2DParams params = Vkm::GL::renderTargetParams(
                size,
                size,
                format,
                GL_RED,
                Vkm::GL::TextureMinFilter::Linear,
                Vkm::GL::TextureMagFilter::Linear
            );
            return std::make_unique<Vkm::GL::Texture2D>(name, params);
        };
        m_height     = texture("ocean_shore_height", GL_R16F, SHORE_WATER);
        m_under      = texture("ocean_shore_under", GL_R16F);
        m_underDepth = texture("ocean_shore_depth", GL_DEPTH_COMPONENT24);
        m_over       = texture("ocean_shore_over", GL_R16F);
        m_overDepth  = texture("ocean_shore_over_depth", GL_DEPTH_COMPONENT24);
        m_half       = texture("ocean_shore_half", GL_R32F);
        m_map        = texture("ocean_shore", GL_R16F);
        m_underTarget.attachTexture(GL_COLOR_ATTACHMENT0, m_under->getID());
        m_underTarget.attachTexture(GL_DEPTH_ATTACHMENT, m_underDepth->getID());
        m_underTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
        m_overTarget.attachTexture(GL_COLOR_ATTACHMENT0, m_over->getID());
        m_overTarget.attachTexture(GL_DEPTH_ATTACHMENT, m_overDepth->getID());
        m_overTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    // The square, ahead of the eye on the XZ plane, snapped to whole texels so it never swims.
    const float     texel   = SHORE_EDGE / static_cast<float>(SHORE_SIZE);
    const glm::vec3 eye     = ctx.view.camera.position;
    const glm::vec3 forward = -glm::vec3(ctx.view.camera.invView[2]);
    glm::vec2       ahead   = glm::vec2(forward.x, forward.z);
    if (glm::length(ahead) > 1e-3f) ahead = glm::normalize(ahead);
    const glm::vec2 aimed   = glm::vec2(eye.x, eye.z) + SHORE_AHEAD * SHORE_EDGE * ahead;
    const glm::vec2 middle  = glm::round(aimed / texel) * texel;
    const float     half    = 0.5f * SHORE_EDGE;

    // Between the highest the water reaches and SHORE_DEPTH under its lowest.
    const float reach  = ocean.surface.farthestReach().y;
    const float top    = ocean.level + reach;
    const float bottom = ocean.level - reach - SHORE_DEPTH;

    // What lies over the square but the water and the cut-outs: a cut-out's card is mostly air, which
    // the shore, reading no texture, would take for solid.
    const RenderObjects& objects = *ctx.view.objects;
    m_objects.clear();
    for (const uint32_t object : objects.scene) {
        const Math::AABB& box = objects.bounds[object];
        if (box.max.x < middle.x - half || box.min.x > middle.x + half) continue;
        if (box.max.z < middle.y - half || box.min.z > middle.y + half) continue;
        if (box.max.y < bottom || box.min.y > top) continue;
        if (objects.draws[object].waves) continue;
        const GLMaterial* material = ctx.resources.getMaterial(objects.draws[object].material);
        if (material && material->getType() == MaterialType::AlphaMask) continue;
        m_objects.push_back(object);
    }

    // Looking straight down from the top, screen up toward -Z: row 0 is the square's most Z.
    const glm::vec3 above = glm::vec3(middle.x, top, middle.y);
    const glm::vec3 down  = glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::mat4 view  = glm::lookAt(above, above + down, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::mat4 shoreVP = glm::ortho(-half, half, -half, half, 0.0f, top - bottom) * view;
    const glm::vec4 area    = glm::vec4(middle.x - half, middle.y + half, SHORE_EDGE, 1.0f);

    // The water's height over the square, which the draw reads in place of the sum.
    const uint32_t waterGroups = Groups::covering(static_cast<uint32_t>(SHORE_WATER), Groups::IMAGE);
    m_water.bind();
    m_water.setUniform4fv("u_area", area);
    m_height->bindImage(0, GL_WRITE_ONLY);
    m_water.dispatch(waterGroups, waterGroups);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    // Under the water the topmost thing, over it the lowest: the depth looks down in the shadow
    // convention, so the one keeps the least depth, as a shadow map does, and the other the most.
    const float nothing = SHORE_NOTHING;
    const float least   = 0.0f;
    ctx.gl.setViewport(0, 0, SHORE_SIZE, SHORE_SIZE);
    clearTarget(m_underTarget, nothing, GLDepth::SHADOW_CLEAR);
    clearTarget(m_overTarget, nothing, least);

    if (!m_objects.empty()) {
        const bool posed = ctx.skinPalette.count() > 0;
        m_batcher.buildGrouped(m_objects, objects, ctx.resources, ctx.skinPalette.count());
        ctx.objects.bind();
        if (posed) ctx.skinPalette.bind();
        m_height->bindSlot(0);
        for (const bool over : {false, true}) {
            (over ? m_overTarget : m_underTarget).bind();
            ctx.gl.setDepthFunc(over ? GL_GREATER : GLDepth::SHADOW_TEST);
            const Vkm::GL::Shader* bound = nullptr;
            for (const InstanceDraw& draw : m_batcher.draws()) {
                Vkm::GL::Shader& program = draw.skinned ? m_drawSkinned : m_draw;
                if (&program != bound) {
                    program.bind();
                    program.setUniformMatrix4fv("u_shoreVP", GLDepth::shadowProjection(shoreVP));
                    program.setUniform1f("u_level", ocean.level);
                    program.setUniform4fv("u_area", area);
                    program.setUniform1i("u_drawOver", over ? 1 : 0);
                    program.setUniform1f("u_nothing", SHORE_NOTHING);
                    bound = &program;
                }
                m_batcher.draw(ctx.gl, draw);
            }
        }
    }

    // The distances, a row then a column at a time.
    const int wanted      = static_cast<int>(std::ceil(SHORE_REACH * ocean.waterline / texel));
    const int reachTexels = std::clamp(wanted, 1, SHORE_MOST);
    const uint32_t groups = Groups::covering(static_cast<uint32_t>(SHORE_SIZE), Groups::IMAGE);
    m_rows.bind();
    m_rows.setUniform1f("u_texel", texel);
    m_rows.setUniform1i("u_reach", reachTexels);
    m_under->bindSlot(0);
    m_over->bindSlot(1);
    m_half->bindImage(0, GL_WRITE_ONLY);
    m_rows.dispatch(groups, groups);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    m_columns.bind();
    m_columns.setUniform1f("u_texel", texel);
    m_columns.setUniform1i("u_reach", reachTexels);
    m_half->bindSlot(0);
    m_map->bindImage(0, GL_WRITE_ONLY);
    m_columns.dispatch(groups, groups);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    m_area   = glm::vec4(glm::vec3(area), static_cast<float>(reachTexels) * texel);
    m_shored = true;
}

void GLOceanShore::bind() const {
    if (m_shored) m_map->bindSlot(GLBindings::OceanTextureSlots::SHORE);
}

} // namespace Vkm::Engine
