#include "storage/gl_ocean_fields.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <GL/glew.h>

#include "gl_error_handle.h"
#include "gl_texture.h"

#include "gl_context.h"
#include "gl_frame_context.h"
#include "gl_mip_levels.h"
#include "gl_view.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "convention/gl_bindings.h"
#include "frame/gl_ocean.h"
#include "ecs/component/render/ocean.h"
#include "system/render/data/render_objects.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Groups = GLBindings::ComputeGroups;

// Anisotropic taps along a grazing view of the sea.
constexpr float MAX_ANISOTROPY = 8.0f;
// The shore's square: metres on an edge, texels across it, and how far ahead of the eye its
// middle lies, in edges - most of the sea the eye sees is ahead of it.
constexpr float SHORE_EDGE  = Ocean::WATERLINE_SQUARE;
constexpr int   SHORE_SIZE  = 512;
constexpr float SHORE_AHEAD = 0.3f;
// Texels across the water's height over the square: a metre, filtered, against waves of a few
// metres and more (Ocean::minWavelength), whose shortest stand a few centimetres high.
constexpr int   SHORE_WATER = 128;
// How far round a point the distance transform looks, in waterline widths, at most SHORE_MOST
// texels: the foam keeps inside it, whatever the waves and suds ask (ocean.glsl, oceanWaterline).
constexpr float SHORE_REACH = 2.0f;
constexpr int   SHORE_MOST  = 32;
// What the shore holds where nothing lies under the water: far past any reach.
constexpr float SHORE_NOTHING = 10000.0f;
// Metres under the water's lowest the shore still draws: deeper counts past any reach.
constexpr float SHORE_DEPTH = 2.0f;

// Binds @p texture to texture unit @p unit, as the 4.3 context does it: no direct state access.
void bindToUnit(uint32_t unit, GLenum target, uint32_t texture) {
    VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    VKM_GL_CHECK(glBindTexture(target, texture));
}

// Clears @p target's colour to @p colour and its depth to @p depth.
void clearTarget(const Vkm::GL::FrameBuffer& target, float colour, float depth) {
    target.bind();
    VKM_GL_CHECK(glClearBufferfv(GL_COLOR, 0, &colour));
    VKM_GL_CHECK(glClearBufferfv(GL_DEPTH, 0, &depth));
}

} // namespace

GLOceanFields::GLOceanFields()
    : m_ripples("shaders/ocean/ripples")
    , m_ripplesColumns("shaders/ocean/ripples_columns")
    , m_ripplesDown("shaders/ocean/ripples_down")
    , m_shore("shaders/ocean/shore")
    , m_shoreSkinned("shaders/ocean/shore_skinned")
    , m_shoreWater("shaders/ocean/shore_water")
    , m_shoreRows("shaders/ocean/shore_rows")
    , m_shoreColumns("shaders/ocean/shore_columns") {}

GLOceanFields::~GLOceanFields() {
    if (m_rows) glDeleteTextures(1, &m_rows);
    if (m_fields) glDeleteTextures(1, &m_fields);
    const uint32_t textures[] = {
        m_shoreHeight,
        m_shoreUnder,
        m_shoreDepth,
        m_shoreOver,
        m_shoreOverDepth,
        m_shoreHalf,
        m_shoreMap,
    };
    for (const uint32_t texture : textures) {
        if (texture) glDeleteTextures(1, &texture);
    }
}

void GLOceanFields::beginFrame() {
    m_rippled = false;
    m_shored  = false;
}

void GLOceanFields::drawRipples() {
    if (!m_fields) {
        constexpr int size = RIPPLE_FIELD_SIZE;
        VKM_GL_CHECK(glGenTextures(1, &m_rows));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_rows));
        VKM_GL_CHECK(glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA32F, size, size, RIPPLE_FIELDS));
        m_levels = mipLevelsDownToTwo(size, size, 16);
        VKM_GL_CHECK(glGenTextures(1, &m_fields));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_fields));
        VKM_GL_CHECK(glTexStorage3D(GL_TEXTURE_2D_ARRAY, m_levels, GL_RGBA16F, size, size, RIPPLE_FIELDS));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        const float anisotropy = std::min(Vkm::GL::maxSupportedAnisotropy(), MAX_ANISOTROPY);
        if (anisotropy > 1.0f) {
            VKM_GL_CHECK(glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, anisotropy));
        }
    }

    // A row of every field a group, then a column: both transforms of a line run in shared memory.
    const uint32_t lines = static_cast<uint32_t>(RIPPLE_FIELD_SIZE);
    const uint32_t layers = static_cast<uint32_t>(RIPPLE_FIELDS);
    m_ripples.bind();
    VKM_GL_CHECK(glBindImageTexture(0, m_rows, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F));
    m_ripples.dispatch(1, lines, layers);
    VKM_GL_CHECK(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));

    m_ripplesColumns.bind();
    VKM_GL_CHECK(glBindImageTexture(0, m_rows, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F));
    VKM_GL_CHECK(glBindImageTexture(1, m_fields, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F));
    m_ripplesColumns.dispatch(lines, 1, layers);
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));

    // The levels below, every layer at a dispatch: the driver's own mip build costs more.
    m_ripplesDown.bind();
    bindToUnit(0, GL_TEXTURE_2D_ARRAY, m_fields);
    for (int level = 1; level < m_levels; ++level) {
        const uint32_t size = static_cast<uint32_t>(RIPPLE_FIELD_SIZE >> level);
        m_ripplesDown.setUniform1i("u_above", level - 1);
        VKM_GL_CHECK(glBindImageTexture(0, m_fields, level, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F));
        m_ripplesDown.dispatch(
            Groups::covering(size, Groups::IMAGE),
            Groups::covering(size, Groups::IMAGE),
            static_cast<uint32_t>(RIPPLE_FIELDS)
        );
        VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
    }
    m_rippled = true;
}

void GLOceanFields::captureShore(GLFrameContext& ctx) {
    const OceanData& ocean = ctx.view.ocean;
    if (!m_shoreMap) {
        const auto texture = [](uint32_t& id, GLenum format, int size = SHORE_SIZE) {
            VKM_GL_CHECK(glGenTextures(1, &id));
            VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D, id));
            VKM_GL_CHECK(glTexStorage2D(GL_TEXTURE_2D, 1, format, size, size));
            VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        };
        texture(m_shoreHeight, GL_R16F, SHORE_WATER);
        texture(m_shoreUnder, GL_R16F);
        texture(m_shoreDepth, GL_DEPTH_COMPONENT24);
        texture(m_shoreOver, GL_R16F);
        texture(m_shoreOverDepth, GL_DEPTH_COMPONENT24);
        texture(m_shoreHalf, GL_R32F);
        texture(m_shoreMap, GL_R16F);
        m_shoreTarget.bind();
        m_shoreTarget.attachTexture2D(GL_COLOR_ATTACHMENT0, m_shoreUnder);
        m_shoreTarget.attachTexture2D(GL_DEPTH_ATTACHMENT, m_shoreDepth);
        m_shoreTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
        m_shoreOverTarget.bind();
        m_shoreOverTarget.attachTexture2D(GL_COLOR_ATTACHMENT0, m_shoreOver);
        m_shoreOverTarget.attachTexture2D(GL_DEPTH_ATTACHMENT, m_shoreOverDepth);
        m_shoreOverTarget.setDrawBuffer(GL_COLOR_ATTACHMENT0);
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

    // What lies over the square but the water.
    const RenderObjects& objects = *ctx.view.objects;
    m_shoreObjects.clear();
    for (const uint32_t object : objects.scene) {
        const Math::AABB& box = objects.bounds[object];
        if (box.max.x < middle.x - half || box.min.x > middle.x + half) continue;
        if (box.max.z < middle.y - half || box.min.z > middle.y + half) continue;
        if (box.max.y < bottom || box.min.y > top) continue;
        if (objects.draws[object].waves) continue;
        m_shoreObjects.push_back(object);
    }

    // Looking straight down from the top, screen up toward -Z: row 0 is the square's most Z.
    const glm::vec3 above = glm::vec3(middle.x, top, middle.y);
    const glm::vec3 down  = glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::mat4 view  = glm::lookAt(above, above + down, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::mat4 shoreVP = glm::ortho(-half, half, -half, half, 0.0f, top - bottom) * view;
    const glm::vec4 area    = glm::vec4(middle.x - half, middle.y + half, SHORE_EDGE, 1.0f);

    // The water's height over the square, which the draw reads in place of the sum.
    const uint32_t waterGroups = Groups::covering(static_cast<uint32_t>(SHORE_WATER), Groups::IMAGE);
    m_shoreWater.bind();
    m_shoreWater.setUniform4fv("u_area", area);
    VKM_GL_CHECK(glBindImageTexture(0, m_shoreHeight, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F));
    m_shoreWater.dispatch(waterGroups, waterGroups);
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));

    // Under the water the topmost thing, over it the lowest: the depth looks down, so the one
    // keeps the least depth and the other the most.
    const float nothing  = SHORE_NOTHING;
    const float nearest  = 1.0f;
    const float farthest = 0.0f;
    ctx.gl.setViewport(0, 0, SHORE_SIZE, SHORE_SIZE);
    clearTarget(m_shoreTarget, nothing, nearest);
    clearTarget(m_shoreOverTarget, nothing, farthest);

    if (!m_shoreObjects.empty()) {
        const bool posed = ctx.skinPalette.count() > 0;
        m_shoreBatcher.buildGrouped(m_shoreObjects, objects, ctx.resources, ctx.skinPalette.count());
        ctx.objects.bind();
        if (posed) ctx.skinPalette.bind();
        bindToUnit(0, GL_TEXTURE_2D, m_shoreHeight);
        for (const bool over : {false, true}) {
            (over ? m_shoreOverTarget : m_shoreTarget).bind();
            ctx.gl.setDepthFunc(over ? GL_GREATER : GL_LESS);
            const Vkm::GL::Shader* bound = nullptr;
            for (const InstanceDraw& draw : m_shoreBatcher.draws()) {
                Vkm::GL::Shader& program = draw.skinned ? m_shoreSkinned : m_shore;
                if (&program != bound) {
                    program.bind();
                    program.setUniformMatrix4fv("u_shoreVP", shoreVP);
                    program.setUniform1f("u_level", ocean.level);
                    program.setUniform4fv("u_area", area);
                    program.setUniform1i("u_drawOver", over ? 1 : 0);
                    program.setUniform1f("u_nothing", SHORE_NOTHING);
                    bound = &program;
                }
                m_shoreBatcher.draw(ctx.gl, draw);
            }
        }
    }

    // The distances, a row then a column at a time.
    const int wanted      = static_cast<int>(std::ceil(SHORE_REACH * ocean.waterline / texel));
    const int reachTexels = std::clamp(wanted, 1, SHORE_MOST);
    const uint32_t groups = Groups::covering(static_cast<uint32_t>(SHORE_SIZE), Groups::IMAGE);
    m_shoreRows.bind();
    m_shoreRows.setUniform1f("u_texel", texel);
    m_shoreRows.setUniform1i("u_reach", reachTexels);
    bindToUnit(0, GL_TEXTURE_2D, m_shoreUnder);
    bindToUnit(1, GL_TEXTURE_2D, m_shoreOver);
    VKM_GL_CHECK(glBindImageTexture(0, m_shoreHalf, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F));
    m_shoreRows.dispatch(groups, groups);
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));

    m_shoreColumns.bind();
    m_shoreColumns.setUniform1f("u_texel", texel);
    m_shoreColumns.setUniform1i("u_reach", reachTexels);
    bindToUnit(0, GL_TEXTURE_2D, m_shoreHalf);
    VKM_GL_CHECK(glBindImageTexture(0, m_shoreMap, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F));
    m_shoreColumns.dispatch(groups, groups);
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));

    m_shoreArea   = glm::vec4(glm::vec3(area), static_cast<float>(reachTexels) * texel);
    m_shored      = true;
}

void GLOceanFields::bind() const {
    if (m_rippled) bindToUnit(GLBindings::OceanTextureSlots::RIPPLES, GL_TEXTURE_2D_ARRAY, m_fields);
    if (m_shored) bindToUnit(GLBindings::OceanTextureSlots::SHORE, GL_TEXTURE_2D, m_shoreMap);
}

} // namespace Vkm::Engine
