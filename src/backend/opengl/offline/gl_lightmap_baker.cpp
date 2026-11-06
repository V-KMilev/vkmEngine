#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_lightmap_baker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "vkm_log/logger.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/texture/cube_faces.h"

#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "base/hash/fnv1a.h"
#include "base/math/aabb.h"
#include "base/math/axes.h"
#include "base/profile/profiler.h"
#include "frame/light/gl_shadow_shape.h"
#include "frame/gl_object_buffer.h"
#include "storage/light/gl_lightmaps.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

static_assert(
    GLBindings::ProbeTextureSlots::MAX_PROBES
        <= GLBindings::ComputeGroups::IMAGE * GLBindings::ComputeGroups::IMAGE,
    "the probes' centers are one group's"
);

static_assert(
    CAPTURE_SETTLE_FRAMES > GLLightmaps::RETIRE_FRAMES,
    "a bake is planned only once what the last one let go of is freed"
);

namespace {

// A local light's maps start this near it, meters.
constexpr float MAP_NEAR = 0.05f;

// A point light's faces, each wider than a quarter turn so a filter disk near an edge stays on it.
const float POINT_FACE_FOV = glm::radians(100.0f);

// Meters the sun's map reaches past the receivers it covers, so a caster at their edge is in it.
constexpr float SUN_MARGIN = 2.0f;

// The way toward the sky of direction @p i of the sky's bake: a Fibonacci spiral over the dome,
// even in solid angle, so each stands for as much of the sky. Their heights sum to half their count.
glm::vec3 skyward(uint32_t i) {
    const float y    = (static_cast<float>(i) + 0.5f) / static_cast<float>(GLLightmapBaker::SKY_DIRECTIONS);
    const float r    = std::sqrt(1.0f - y * y);
    const float turn = glm::pi<float>() * (3.0f - std::sqrt(5.0f)) * static_cast<float>(i);
    return {r * std::cos(turn), y, r * std::sin(turn)};
}

// Whether object @p o of @p view takes a lightmap: keyed, fixed (ObjectDraw::fixed), the mesh it was
// given (RenderObjects::bases) unwrapped and its material Opaque, and not water, which the eye draws
// with live shadows alone.
bool receives(const RenderView& view, const GLAssetMirror& mirror, uint32_t o) {
    if (o >= view.objects->keys.size() || o >= view.objects->bases.size()) return false;
    const ObjectDraw& draw = view.objects->draws[o];
    if (!draw.fixed) return false;
    const GLMesh* mesh = mirror.getMesh(view.objects->bases[o]);
    if (!mesh || !mesh->lightmapped() || mesh->layout() != VertexLayout::Static) return false;
    const GLMaterial* material = mirror.getMaterial(draw.material);
    return material && material->getType() == MaterialType::Opaque && draw.water == 0;
}

// A 2D texture of @p format, @p side a side, read texel for texel.
std::unique_ptr<Vkm::OpenGL::Texture2D> makeTarget(const char* name, GLenum format, uint32_t side) {
    const Vkm::OpenGL::Texture2DParams params = Vkm::OpenGL::renderTargetParams(side, side, format, GL_RGBA);
    return std::make_unique<Vkm::OpenGL::Texture2D>(name, params);
}

} // namespace

GLLightmapBaker::GLLightmapBaker()
    : m_raster("shaders/opengl/offline/lightmap/raster")
    , m_dilate("shaders/opengl/offline/lightmap/dilate")
    , m_shadow("shaders/opengl/offline/lightmap/shadow") {
    // A float a probe: made once, small enough to keep.
    constexpr uint32_t bytes = GLBindings::ProbeTextureSlots::MAX_PROBES * sizeof(float);
    const std::vector<float> open(GLBindings::ProbeTextureSlots::MAX_PROBES, 1.0f);
    m_probeSky  = std::make_unique<Vkm::OpenGL::ShaderStorageBuffer>(open.data(), bytes);
    m_probeRead = std::make_unique<Vkm::OpenGL::ReadbackBuffer>(bytes);
}

GLLightmapBaker::~GLLightmapBaker() {
    releaseScratch();
}

void GLLightmapBaker::releaseScratch() {
    // Detached first: a texture attached to a framebuffer that is not bound outlives its delete.
    m_pageTarget.attachTexture(GL_COLOR_ATTACHMENT0, 0);
    m_pageTarget.attachTexture(GL_COLOR_ATTACHMENT1, 0);
    m_mapTarget.attachTexture(GL_DEPTH_ATTACHMENT, 0);
    for (int i = 0; i < 2; ++i) {
        m_position[i].reset();
        m_normal[i].reset();
    }
    m_maps.reset();
    m_spentMaps.reset();
    m_skyLeft.reset();
    m_releaseIn = 0;
}

void GLLightmapBaker::invalidate() {
    releaseScratch();
    m_probeRead->drop();
    m_readSlots.clear();
    m_indexed = false;
    m_begun   = true;
    m_places.clear();
    m_channels.clear();
    m_baking     = false;
    m_partial    = false;
    m_fixedKnown = false;
    m_baked.reset();
    m_lightRest.clear();
    m_owedLights.clear();
    m_owed = false;
    m_shownPageKeys.clear();
}

void GLLightmapBaker::update(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    GLLightmaps& lightmaps
) {
    if (!view.objects) return;
    // The probes' sky of the last bake, once its copy has landed: read without waiting on it.
    if (m_probeRead->arrived()) {
        const auto* sky = static_cast<const float*>(m_probeRead->data());
        lightmaps.setProbeSky(m_readSlots, std::vector<float>(sky, sky + m_readSlots.size()));
    }
    const uint64_t ask = askOf(view, mirror);
    m_settle.see(ask);
    watchLights(view, lightmaps);
    // A change under way is baked over once it settles: what was planned is of another world.
    if (m_baking && ask != m_bakingAsk) {
        lightmaps.dropBake();
        m_releaseIn = GLLightmaps::RETIRE_FRAMES;
        m_baking    = false;
    }
    // What a bake drew with is freed once the frames in flight are done with it.
    if (!m_baking && m_releaseIn > 0 && --m_releaseIn == 0) releaseScratch();
    if (!m_baking && m_settle.settled() && (m_baked != ask || m_owed)) {
        m_bakingAsk = ask;
        m_partial   = m_baked == ask;
        if (m_partial) {
            planChannels(view, mirror, lightmaps);
        } else if (!plan(view, mirror, lightmaps)) {
            m_baked = ask;
            m_shownPageKeys.clear();
            return;
        }
        // With a bake budget the set begins the next frame: the plan is this one's share.
        if (view.bakeBudget > 0.0f) return;
    }
    if (m_baking) slice(gl, view, mirror, objects, lightmaps, view.bakeBudget > 0.0f);
}

RenderReadiness GLLightmapBaker::progress() const {
    if (m_baking) return {m_stepsDone, m_steps};
    return pending() ? RenderReadiness{0, 1} : RenderReadiness{};
}

void GLLightmapBaker::index(const RenderObjects& scene) {
    // As GLLightmaps keeps its copy: the indices the cull changed alone, else whole.
    const size_t count = scene.keys.size();
    const bool   held  = m_indexed && count == m_keyOf.size();
    if (held && scene.serial == m_indexSerial) return;
    if (held && scene.serial == m_indexSerial + 1 && !scene.allChanged) {
        for (const uint32_t o : scene.changed) {
            if (o >= count || m_keyOf[o] == scene.keys[o]) continue;
            const auto was = m_indexOf.find(m_keyOf[o]);
            if (was != m_indexOf.end() && was->second == o) m_indexOf.erase(was);
            m_keyOf[o]               = scene.keys[o];
            m_indexOf[scene.keys[o]] = o;
        }
    } else {
        m_keyOf = scene.keys;
        m_indexOf.clear();
        for (uint32_t o = 0; o < count; ++o) m_indexOf.emplace(scene.keys[o], o);
    }
    m_indexSerial = scene.serial;
    m_indexed     = true;
}

const std::vector<uint32_t>& GLLightmapBaker::resolve(const std::vector<uint64_t>& keys) {
    m_resolved.clear();
    for (const uint64_t key : keys) {
        const auto found = m_indexOf.find(key);
        if (found != m_indexOf.end()) m_resolved.push_back(found->second);
    }
    return m_resolved;
}

uint64_t GLLightmapBaker::askOf(const RenderView& view, const GLAssetMirror& mirror) {
    // The fixed meshes' part is worked out again only when it may have changed: the cull moves
    // fixedSerial when a fixed mesh does, and the mirror its revision when an upload lands. A frame
    // where everything may differ is not held whatever the serial says: it is another cull's count.
    const RenderObjects& objects = *view.objects;
    const bool held = m_fixedKnown && !objects.allChanged && objects.fixedSerial == m_fixedSerial
        && mirror.syncedRevision() == m_fixedRevision;
    if (!held) {
        m_fixedDigest   = fixedDigest(objects, mirror);
        m_fixedSerial   = objects.fixedSerial;
        m_fixedRevision = mirror.syncedRevision();
        m_fixedKnown    = true;
    }
    // Chained in the channels' order: a light taking another's channel is another bake. Where each
    // stands is its channel's own (watchLights).
    uint64_t lights = FNV1A_OFFSET_BASIS;
    GLLightmaps::bakedLights(view.lists.lights, m_lights);
    for (const uint32_t i : m_lights) {
        const uint32_t slot = view.lists.lights[i].entitySlot;
        lights = fnv1a64Bytes(&slot, sizeof(slot), lights);
    }
    return fnv1a64Bytes(&lights, sizeof(lights), fnv1a64Bytes(&m_fixedDigest, sizeof(m_fixedDigest)));
}

void GLLightmapBaker::watchLights(const RenderView& view, const GLLightmaps& lightmaps) {
    // m_lights is askOf's this frame: the lights the bake takes, in channel order.
    if (m_lightRest.size() != m_lights.size()) m_lightRest.resize(m_lights.size());
    m_owedLights.clear();
    for (uint32_t place = 0; place < m_lights.size(); ++place) {
        const LightData& light = view.lists.lights[m_lights[place]];
        auto& [slot, rest]     = m_lightRest[place];
        if (slot != light.entitySlot) {
            slot = light.entitySlot;
            rest = CaptureSettle<uint64_t>{};
        }
        const uint64_t shape = GLLightmaps::shapeOf(light);
        rest.see(shape);
        if (rest.settled() && !lightmaps.holdsChannel(slot, static_cast<int>(place), shape)) {
            m_owedLights.push_back(place);
        }
    }
    // Owed alone only over a bake of the fixed world as it is, else the whole bake takes them, and
    // one with pages: where nothing receives, no channel holds anything.
    m_owed = !m_owedLights.empty() && m_baked == m_settle.last() && !m_shownPageKeys.empty();
}

bool GLLightmapBaker::stillLight(uint32_t place) const {
    return place < m_lightRest.size() && m_lightRest[place].second.settled();
}

void GLLightmapBaker::fixedExtent(
    const RenderView& view,
    const GLAssetMirror& mirror,
    Math::AABB& receivers,
    std::vector<uint32_t>& casters
) {
    const RenderObjects& scene = *view.objects;
    const float most = std::numeric_limits<float>::max();
    receivers = Math::AABB{glm::vec3(most), glm::vec3(-most)};
    for (const uint32_t o : scene.scene) {
        if (!receives(view, mirror, o)) continue;
        receivers.min = glm::min(receivers.min, scene.bounds[o].min);
        receivers.max = glm::max(receivers.max, scene.bounds[o].max);
    }
    // What casts: fixed, and in the casting prefix of the scene list.
    casters.clear();
    for (uint32_t i = 0; i < scene.casterCount && i < scene.scene.size(); ++i) {
        const uint32_t o = scene.scene[i];
        if (scene.draws[o].fixed) casters.push_back(o);
    }
}

uint64_t GLLightmapBaker::fixedDigest(const RenderObjects& objects, const GLAssetMirror& mirror) {
    // Summed, not chained: what a bake is made from does not change when an index or the order
    // does, as meshes come and go.
    uint64_t fixed = 0;
    for (size_t i = 0; i < objects.scene.size(); ++i) {
        const uint32_t o = objects.scene[i];
        const ObjectDraw& draw = objects.draws[o];
        if (!draw.fixed || o >= objects.keys.size() || o >= objects.bases.size()) continue;
        // Whether the mesh it was given has landed, and with lightmap coordinates, not which mesh:
        // the key and its shape name it. The bake draws it, not the level the eye picked, so a level
        // swapped in as the eye moves changes nothing baked; a caster not yet landed casts nothing.
        const GLMesh* mesh = mirror.getMesh(objects.bases[o]);
        const bool landed  = mesh != nullptr;
        const bool unwraps = landed && mesh->lightmapped();
        const bool casts   = i < objects.casterCount;
        uint64_t one = fnv1a64Bytes(&objects.keys[o], sizeof(objects.keys[o]));
        one = fnv1a64Bytes(&landed, sizeof(landed), one);
        one = fnv1a64Bytes(&unwraps, sizeof(unwraps), one);
        one = fnv1a64Bytes(&casts, sizeof(casts), one);
        one = fnv1a64Bytes(&objects.models[o], sizeof(objects.models[o]), one);
        // Its shape and its surface as they are now: a sculpt, a re-import or a material made a
        // cutout keeps every handle.
        const uint64_t shape = o < objects.shapes.size() ? objects.shapes[o] : 0;
        const GLMaterial* surface = mirror.getMaterial(draw.material);
        const uint64_t looks = surface ? surface->uploadId() : 0;

        one = fnv1a64Bytes(&shape, sizeof(shape), one);
        one = fnv1a64Bytes(&looks, sizeof(looks), one);
        fixed += fnv1a64Bytes(&draw.material, sizeof(draw.material), one);
    }
    return fixed;
}

GLLightmapBaker::ChannelPlan GLLightmapBaker::planLight(
    const LightData& light,
    int channel,
    const RenderObjects& scene,
    const Math::AABB& receiverBounds,
    const std::vector<uint32_t>& casters
) {
    LightKind kind = LightKind::Sun;
    int layers = 1;
    float source     = 0.0f;
    float texelWorld = 0.0f;
    uint32_t side = LOCAL_MAP;

    // The views the light's maps are drawn through: a local light's one, the sun's tiles.
    std::vector<MapView> views;
    if (light.type == LightType::Directional) {
        kind       = LightKind::Sun;
        side       = SUN_MAP;
        source     = std::tan(light.sourceRadius);
        texelWorld = SUN_TEXEL;
        const glm::vec3 dir = glm::normalize(light.direction);
        const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), dir, Math::stableUp(dir));
        const Math::AABB across = Math::transform(lightView, receiverBounds);
        const glm::vec2 lo = glm::vec2(across.min) - glm::vec2(SUN_MARGIN);
        const glm::vec2 hi = glm::vec2(across.max) + glm::vec2(SUN_MARGIN);
        // A tile bakes its core; the rest of its map is the reach of the core's texels.
        const float reach = static_cast<float>(REACH_TEXELS) * SUN_TEXEL;
        const float core  = static_cast<float>(SUN_MAP - 2u * REACH_TEXELS) * SUN_TEXEL;
        const glm::ivec2 tiles = glm::max(glm::ivec2(glm::ceil((hi - lo) / core)), glm::ivec2(1));
        for (int ty = 0; ty < tiles.y; ++ty) {
            for (int tx = 0; tx < tiles.x; ++tx) {
                const glm::vec2 from = lo + glm::vec2(tx, ty) * core - glm::vec2(reach);
                const glm::vec2 to   = from + glm::vec2(core + 2.0f * reach);
                MapView& tile = views.emplace_back();
                float top  = -std::numeric_limits<float>::max();
                float base = std::numeric_limits<float>::max();
                for (const uint32_t o : casters) {
                    const Math::AABB c = Math::transform(lightView, scene.bounds[o]);
                    if (c.max.x < from.x || c.min.x > to.x || c.max.y < from.y || c.min.y > to.y) {
                        continue;
                    }
                    top  = std::max(top, c.max.z);
                    base = std::min(base, c.min.z);
                    tile.casters.push_back(scene.keys[o]);
                }
                if (tile.casters.empty()) {
                    views.pop_back();  // nothing to cast: its texels stay lit
                    continue;
                }
                // Out to the farthest receiver too: one that casts nothing, under a roof far above
                // it, lies past every caster, and past the map's far plane it would read lit.
                base = std::min(base, across.min.z);
                const float front = -top - SUN_MARGIN;
                const float back  = -base + SUN_MARGIN;
                const glm::mat4 projection = glm::ortho(from.x, to.x, from.y, to.y, front, back);

                tile.viewProj[0] = GLDepth::shadowProjection(projection * lightView);
                tile.depth       = back - front;
                tile.rim         = static_cast<float>(REACH_TEXELS) / static_cast<float>(SUN_MAP);
            }
        }
    } else {
        MapView& only = views.emplace_back();
        only.depth = std::max(light.radius, MAP_NEAR * 2.0f);
        const float reach = light.radius;
        for (const uint32_t o : casters) {
            const Math::AABB& b     = scene.bounds[o];
            const glm::vec3 nearest = glm::clamp(light.position, b.min, b.max);
            if (glm::distance(nearest, light.position) <= reach) only.casters.push_back(scene.keys[o]);
        }
        source = light.sourceRadius;
        // A spot too wide for one map bakes six faces, as a point light does.
        if (!castsThroughCube(light)) {
            const glm::vec3 dir = glm::normalize(light.direction);
            const float fov     = spotMapFov(light);
            const glm::mat4 v   = glm::lookAt(light.position, light.position + dir, Math::stableUp(dir));
            const glm::mat4 p   = glm::perspective(fov, 1.0f, MAP_NEAR, only.depth);

            kind             = LightKind::Spot;
            only.viewProj[0] = GLDepth::shadowProjection(p * v);
            texelWorld       = 2.0f * std::tan(0.5f * fov) / static_cast<float>(side);
        } else {
            kind   = LightKind::Point;
            layers = 6;
            const glm::mat4 projection = glm::perspective(POINT_FACE_FOV, 1.0f, MAP_NEAR, only.depth);
            for (int f = 0; f < 6; ++f) {
                const glm::mat4 v = Vkm::OpenGL::cubeFaceView(f, light.position);
                only.viewProj[f] = GLDepth::shadowProjection(projection * v);
            }
            texelWorld = 2.0f * std::tan(0.5f * POINT_FACE_FOV) / static_cast<float>(side);
        }
    }

    ChannelPlan planned;
    planned.light      = light;
    planned.channel    = channel;
    planned.kind       = kind;
    planned.layers     = layers;
    planned.side       = side;
    planned.source     = source;
    planned.texelWorld = texelWorld;
    planned.views      = std::move(views);
    return planned;
}

void GLLightmapBaker::restart() {
    // A bake's targets are sized for its pages; a last one's still held are long out of flight.
    releaseScratch();
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    m_started   = std::chrono::duration<double>(since).count();
    m_receivers = 0;
    m_channel   = 0;
    m_view      = 0;
    m_page      = 0;
    m_stepsDone = 0;
    m_frames    = 0;
    m_mapsHold  = -1;
    m_pageHold  = -1;
    m_layer     = 0;
    m_casterAt  = 0;
    m_spread    = -1;
    m_row       = 0;
    m_dir       = 0;
    m_pass      = 0;
}

bool GLLightmapBaker::plan(const RenderView& view, const GLAssetMirror& mirror, GLLightmaps& lightmaps) {
    PROFILE_SCOPE("Lightmap/Plan");
    restart();
    const RenderObjects& scene = *view.objects;
    const uint32_t page = GLLightmaps::PAGE_SIZE;

    // The receivers, the largest first, each a square sized by its surface in the world.
    std::vector<std::pair<uint32_t, uint32_t>> squares;
    for (const uint32_t o : scene.scene) {
        if (!receives(view, mirror, o)) continue;
        // Each axis's share by how far the model stretches the plane across it: a cofactor column.
        const glm::mat3 model(scene.models[o]);
        const glm::vec3 stretch(
            glm::length(glm::cross(model[1], model[2])),
            glm::length(glm::cross(model[2], model[0])),
            glm::length(glm::cross(model[0], model[1]))
        );
        const float    area = glm::dot(mirror.getMesh(scene.bases[o])->lightmapArea(), stretch);
        const float    side = std::sqrt(area) * TEXELS_PER_METER;
        const uint32_t even = (static_cast<uint32_t>(std::ceil(side)) + 3u) & ~3u;
        squares.emplace_back(o, std::clamp(even, MIN_SIDE, MAX_SIDE));
    }
    const auto larger = [](const auto& a, const auto& b) { return a.second > b.second; };
    std::sort(squares.begin(), squares.end(), larger);

    // The lights baked, a channel each by their place in m_lights (askOf's). One still moving is drawn
    // live, and its channel baked once it holds still.
    std::vector<GLLightmaps::BakedLight> channels;
    for (uint32_t place = 0; place < m_lights.size(); ++place) {
        if (!stillLight(place)) continue;
        const LightData& light = view.lists.lights[m_lights[place]];
        channels.push_back({light.entitySlot, static_cast<int>(place), GLLightmaps::shapeOf(light)});
    }
    if (squares.empty()) {
        lightmaps.beginBake(scene, mirror, {}, 0, {});
        lightmaps.finishBake();
        return false;
    }

    // Shelves across the pages, a row the height of its first, tallest square.
    GLLightmaps::Places places;
    m_pageKeys.assign(1, {});
    uint32_t x   = 0;
    uint32_t y   = 0;
    uint32_t row = 0;
    for (const auto& [o, side] : squares) {
        if (x + side > page) {
            x = 0;
            y += row;
            row = 0;
        }
        if (y + side > page) {
            m_pageKeys.emplace_back();
            x   = 0;
            y   = 0;
            row = 0;
        }
        // Inside a gutter the spread fills, so a filter at a square's edge never reads the next one.
        const float to    = 1.0f / static_cast<float>(page);
        const float inner = static_cast<float>(side - 2u * GUTTER) * to;
        GLLightmaps::Place& placed = places[scene.keys[o]];
        placed.lightmap.scaleOffset = glm::vec4(glm::vec2(inner), glm::vec2(x + GUTTER, y + GUTTER) * to);
        placed.lightmap.page        = glm::ivec4(static_cast<int>(m_pageKeys.size()) - 1, 0, 0, 0);
        placed.model                = scene.models[o];
        m_pageKeys.back().push_back(scene.keys[o]);
        x += side;
        row = std::max(row, side);
    }
    const auto pageCount = static_cast<uint32_t>(m_pageKeys.size());
    // The set begins on the next slice: the frame after this one while the frame has a bake budget.
    m_places   = std::move(places);
    m_channels = std::move(channels);
    m_begun    = false;

    Math::AABB            receiverBounds;
    std::vector<uint32_t> casters;
    fixedExtent(view, mirror, receiverBounds, casters);

    m_plan.clear();
    for (const GLLightmaps::BakedLight& baked : m_channels) {
        const LightData& light = view.lists.lights[m_lights[static_cast<size_t>(baked.channel)]];
        m_plan.push_back(planLight(light, baked.channel, scene, receiverBounds, casters));
    }

    // The sky: an orthographic map a direction, each over every receiver and through every caster
    // above them, all square at the one texel the widest needs.
    Math::AABB casterBounds = receiverBounds;
    for (const uint32_t o : casters) {
        casterBounds.min = glm::min(casterBounds.min, scene.bounds[o].min);
        casterBounds.max = glm::max(casterBounds.max, scene.bounds[o].max);
    }
    std::vector<glm::mat4> skyViews;
    float across = 0.0f;
    for (uint32_t i = 0; i < SKY_DIRECTIONS; ++i) {
        const glm::vec3 dir = -skyward(i);
        skyViews.push_back(glm::lookAt(glm::vec3(0.0f), dir, Math::stableUp(dir)));
        const Math::AABB seen = Math::transform(skyViews.back(), receiverBounds);
        across = std::max({across, seen.max.x - seen.min.x, seen.max.y - seen.min.y});
    }
    across += 2.0f * SUN_MARGIN;

    ChannelPlan& sky = m_plan.emplace_back();
    sky.kind       = LightKind::Sky;
    sky.layers     = 6;
    sky.side       = SKY_MAP;
    sky.texelWorld = across / static_cast<float>(SKY_MAP);
    for (uint32_t i = 0; i < SKY_DIRECTIONS; ++i) {
        if (i % 6 == 0) {
            MapView& added = sky.views.emplace_back();
            for (const uint32_t o : casters) added.casters.push_back(scene.keys[o]);
        }
        const Math::AABB seen = Math::transform(skyViews[i], receiverBounds);
        const float front = -Math::transform(skyViews[i], casterBounds).max.z - SUN_MARGIN;
        const float back  = -seen.min.z + SUN_MARGIN;
        const glm::vec2 lo = 0.5f * (glm::vec2(seen.min) + glm::vec2(seen.max) - glm::vec2(across));
        const glm::vec2 hi = lo + glm::vec2(across);
        const glm::mat4 projection = glm::ortho(lo.x, hi.x, lo.y, hi.y, front, back);
        sky.views.back().viewProj[i % 6] = GLDepth::shadowProjection(projection * skyViews[i]);
    }

    // A step is a view over a page, the sky's a direction over a page.
    m_steps = 0;
    for (const ChannelPlan& planned : m_plan) {
        const auto each = static_cast<uint32_t>(planned.kind == LightKind::Sky ? planned.layers : 1);
        m_steps += static_cast<uint32_t>(planned.views.size()) * pageCount * each;
    }
    // The probes' centers, each open to the sky until a pass hides some of it.
    m_probeSlots.clear();
    m_probeAt.clear();
    for (const ProbeData& probe : view.lists.probes) {
        if (m_probeSlots.size() == GLBindings::ProbeTextureSlots::MAX_PROBES) break;
        m_probeSlots.push_back(probe.entitySlot);
        m_probeAt.push_back(probe.position);
    }
    const std::vector<float> open(GLBindings::ProbeTextureSlots::MAX_PROBES, 1.0f);
    m_probeSky->update(open.data(), static_cast<uint32_t>(open.size() * sizeof(float)));

    m_receivers = static_cast<uint32_t>(squares.size());
    m_baking    = true;
    return true;
}

void GLLightmapBaker::planChannels(
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLLightmaps& lightmaps
) {
    PROFILE_SCOPE("Lightmap/Plan");
    restart();

    Math::AABB            receivers;
    std::vector<uint32_t> casters;
    fixedExtent(view, mirror, receivers, casters);

    // The shown set's lights, the owed ones where they stand now.
    std::vector<GLLightmaps::BakedLight> channels = lightmaps.shownChannels();
    m_plan.clear();
    for (const uint32_t place : m_owedLights) {
        const LightData& light = view.lists.lights[m_lights[place]];
        const auto channel     = static_cast<int>(place);
        const GLLightmaps::BakedLight baked{light.entitySlot, channel, GLLightmaps::shapeOf(light)};
        const auto same = std::find_if(channels.begin(), channels.end(), [&](const auto& held) {
            return held.channel == channel;
        });
        if (same != channels.end()) *same = baked;
        else channels.push_back(baked);
        m_plan.push_back(planLight(light, channel, *view.objects, receivers, casters));
    }

    // A step a page copied over, then a view over a page each.
    m_pageKeys = m_shownPageKeys;
    const auto pageCount = static_cast<uint32_t>(m_pageKeys.size());

    m_steps = pageCount;
    for (const ChannelPlan& planned : m_plan) {
        m_steps += static_cast<uint32_t>(planned.views.size()) * pageCount;
    }
    m_places.clear();
    m_channels = std::move(channels);
    m_begun    = false;
    m_copied   = 0;
    m_probeSlots.clear();
    m_probeAt.clear();
    m_baking = true;
}

void GLLightmapBaker::copyPage(GLLightmaps& lightmaps) const {
    lightmaps.bakingPages().copyLayer(lightmaps.shownPages(), m_copied);
    lightmaps.bakingSky().copyLayer(lightmaps.shownSky(), m_copied);
}

void GLLightmapBaker::slice(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    GLLightmaps& lightmaps,
    bool sliced
) {
    PROFILE_SCOPE("Lightmap/Bake");
    using Wall = std::chrono::steady_clock;
    // While the game plays, a frame takes one piece of a step; under the loading cover, a step.
    const bool playing = sliced && !view.loadingCovers();
    ++m_frames;
    if (!m_begun) {
        if (m_partial) {
            lightmaps.beginPartialBake(*view.objects, mirror, std::move(m_channels));
        } else {
            const auto pageCount = static_cast<uint32_t>(m_pageKeys.size());
            lightmaps.beginBake(*view.objects, mirror, std::move(m_places), pageCount, std::move(m_channels));
        }
        m_begun = true;
        if (playing) return;
    }
    if (m_spentMaps && --m_spentIn == 0) m_spentMaps.reset();
    if (!ensureScratch(playing)) return;
    index(*view.objects);

    // A light none of whose views casts anything has no steps: it is passed over.
    while (m_stepsDone < m_steps) {
        // A partial bake's set starts as a copy of the shown one, a page a step.
        if (m_partial && m_copied < m_pageKeys.size()) {
            copyPage(lightmaps);
            ++m_copied;
            ++m_stepsDone;
            if (playing || sliced) break;
            continue;
        }
        if (m_view >= m_plan[m_channel].views.size()) {
            ++m_channel;
            m_view = 0;
            continue;
        }
        const bool stepped = piece(gl, view, mirror, objects, lightmaps, playing);
        // A step is the GPU's work, not the CPU's: one a frame, which a clock on the CPU would not bound.
        if (playing || (stepped && sliced)) break;
    }
    if (m_stepsDone < m_steps) return;

    lightmaps.finishBake();
    m_releaseIn     = GLLightmaps::RETIRE_FRAMES;
    m_baking        = false;
    m_shownPageKeys = m_pageKeys;
    const double now = std::chrono::duration<double>(Wall::now().time_since_epoch()).count();
    if (m_partial) {
        LOG_INFO(
            "Lightmaps baked again for %zu fixed light(s) that moved, over %zu page(s), in %.0f ms over "
            "%u frame(s)",
            m_plan.size(),
            m_pageKeys.size(),
            (now - m_started) * 1000.0,
            m_frames
        );
        return;
    }
    Vkm::OpenGL::memoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    m_probeRead->capture(*m_probeSky);
    m_readSlots = m_probeSlots;
    m_baked     = m_bakingAsk;
    LOG_INFO(
        "Lightmaps baked: %u receiver(s) on %zu page(s), the sky and %zu fixed light(s), in %.0f ms over "
        "%u frame(s)",
        m_receivers,
        m_pageKeys.size(),
        m_plan.size() - 1,
        (now - m_started) * 1000.0,
        m_frames
    );
}

bool GLLightmapBaker::ensureScratch(bool small) {
    const uint32_t page = GLLightmaps::PAGE_SIZE;
    for (int i = 0; i < 2; ++i) {
        if (!m_position[i]) {
            m_position[i] = makeTarget("lightmap_bake_position", GL_RGBA32F, page);
            if (small) return false;
        }
        if (!m_normal[i]) {
            m_normal[i] = makeTarget("lightmap_bake_normal", GL_RGBA16F, page);
            if (small) return false;
        }
    }
    if (!m_skyLeft) {
        // The sky summed finer than the pages keep it, all of it open until a view hides some.
        Vkm::OpenGL::Texture2DArrayParams params;
        params.width          = page;
        params.height         = page;
        params.layers         = static_cast<uint32_t>(m_pageKeys.size());
        params.internalFormat = GL_R16F;
        m_skyLeft             = std::make_unique<Vkm::OpenGL::Texture2DArray>("lightmap_bake_sky", params);
        const float open = 1.0f;
        m_skyLeft->clear(0, GL_RED, GL_FLOAT, &open);
        if (small) return false;
    }
    return true;
}

bool GLLightmapBaker::piece(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    GLLightmaps& lightmaps,
    bool small
) {
    const RenderObjects& scene = *view.objects;
    const ChannelPlan& planned = m_plan[m_channel];
    const MapView& mapView = planned.views[m_view];
    const uint32_t page = GLLightmaps::PAGE_SIZE;

    // The view's maps: a layer's casters, PLAY_CASTERS of them while small. While small the sky
    // takes its view a direction a pass, through one map, so no frame makes the six at once.
    const bool    sky  = planned.kind == LightKind::Sky;
    const int64_t hold = (static_cast<int64_t>(m_channel) << 40) | (static_cast<int64_t>(m_view) << 8)
        | m_dir;
    if (m_mapsHold != hold) {
        if (m_layer == 0 && m_casterAt == 0) {
            if (m_dir == 0) m_pass = sky && small ? 1 : planned.layers;
            // The sky's maps are orthographic, their depth even across them: at 16 bits a depth step
            // stays a small share of the map texel a compare is lifted by, as both grow with the world.
            const GLenum format = sky ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT32F;
            if (ensureMaps(planned.side, m_pass, format) && small) return false;
        }
        const std::vector<uint32_t>& casters = resolve(mapView.casters);
        const auto count = static_cast<uint32_t>(casters.size());
        const uint32_t from = std::min(m_casterAt, count);
        const uint32_t end  = small ? std::min(count, from + PLAY_CASTERS) : count;
        m_chunk.assign(casters.begin() + from, casters.begin() + end);
        m_mapTarget.bind();
        m_mapTarget.attachLayer(GL_DEPTH_ATTACHMENT, m_maps->getID(), 0, m_layer);
        m_mapTarget.setDrawBuffer(GL_NONE);
        m_mapTarget.setReadBuffer(GL_NONE);
        const glm::mat4& lightVP = mapView.viewProj[m_dir + m_layer];
        const bool fresh = from == 0;
        m_casters.draw(gl, m_chunk, scene, mirror, objects, lightVP, planned.side, MeshOf::Base, fresh);
        m_casterAt = end;
        if (m_casterAt < count) return false;
        m_casterAt = 0;
        if (++m_layer < m_pass) return false;
        m_layer    = 0;
        m_mapsHold = hold;
        // The probes' centers take the pass's directions as the maps are complete: a few points.
        if (sky && !m_probeAt.empty()) {
            passUniforms(planned, mapView);
            m_shadow.setUniform1i("u_probeCount", static_cast<int>(m_probeAt.size()));
            for (size_t i = 0; i < m_probeAt.size(); ++i) {
                const std::string name = "u_probeAt[" + std::to_string(i) + "]";
                m_shadow.setUniform3f(name.c_str(), m_probeAt[i].x, m_probeAt[i].y, m_probeAt[i].z);
            }
            m_maps->bindSlot(GLBindings::LightmapBakeSlots::DEPTH);
            m_probeSky->bindBase(GLBindings::SSBOBindingPoints::LIGHTMAP_PROBE_SKY);
            m_shadow.dispatch(1, 1);
            Vkm::OpenGL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        return false;
    }

    // The page's receivers, then their spread a pass at a time.
    if (m_pageHold != m_page) {
        if (m_spread < 0) {
            lightmaps.bindBakingObjects(scene);
            rasterReceivers(gl, view, mirror, objects, resolve(m_pageKeys[m_page]));
            m_spread = 0;
            if (small) return false;
        }
        while (m_spread < DILATE_PASSES) {
            spread(m_spread++);
            if (small && m_spread < DILATE_PASSES) return false;
        }
        m_spread   = -1;
        m_pageHold = m_page;
        if (small) return false;
    }

    // The page's texels, PLAY_ROWS rows of them while small.
    const LightData& light = planned.light;
    passUniforms(planned, mapView);
    m_shadow.setUniform1i("u_probeCount", 0);
    m_shadow.setUniform1i("u_page", static_cast<int>(m_page));
    m_shadow.setUniform1i("u_channel", planned.channel);
    m_shadow.setUniform3f("u_lightPos", light.position.x, light.position.y, light.position.z);
    const glm::vec3 dir = glm::normalize(light.direction);
    m_shadow.setUniform3f("u_lightDir", dir.x, dir.y, dir.z);
    m_shadow.setUniform1f("u_source", planned.source);
    m_shadow.setUniform1f("u_near", MAP_NEAR);
    m_shadow.setUniform1f("u_far", mapView.depth);
    m_shadow.setUniform1f("u_texelWorld", planned.texelWorld);
    m_shadow.setUniform1f("u_reach", static_cast<float>(REACH_TEXELS));
    m_shadow.setUniform1f("u_rim", mapView.rim);
    // As many texels a piece times the directions a pass takes, however many those are.
    const uint32_t band = PLAY_ROWS * static_cast<uint32_t>(planned.layers / m_pass);
    const uint32_t rows = small ? std::min(band, page - m_row) : page - m_row;
    m_shadow.setUniform2i("u_origin", 0, static_cast<int>(m_row));
    m_position[0]->bindSlot(GLBindings::LightmapBakeSlots::POSITION);
    m_normal[0]->bindSlot(GLBindings::LightmapBakeSlots::NORMAL);
    m_maps->bindSlot(GLBindings::LightmapBakeSlots::DEPTH);
    lightmaps.bakingPages().bindImage(0, GL_READ_WRITE);
    m_skyLeft->bindImage(1, GL_READ_WRITE);
    lightmaps.bakingSky().bindImage(2, GL_WRITE_ONLY);
    namespace Groups = GLBindings::ComputeGroups;
    m_shadow.dispatch(Groups::covering(page, Groups::IMAGE), Groups::covering(rows, Groups::IMAGE));
    Vkm::OpenGL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    m_row += rows;
    if (m_row < page) return false;

    m_row = 0;
    m_stepsDone += static_cast<uint32_t>(sky ? m_pass : 1);
    if (++m_page == static_cast<uint32_t>(m_pageKeys.size())) {
        m_page = 0;
        m_dir += m_pass;
        if (m_dir >= planned.layers) {
            m_dir = 0;
            ++m_view;
        }
    }
    return true;
}

void GLLightmapBaker::passUniforms(const ChannelPlan& planned, const MapView& mapView) {
    m_shadow.bind();
    m_shadow.setUniform1i("u_kind", static_cast<int>(planned.kind));
    for (int layer = 0; layer < m_pass; ++layer) {
        const std::string name = "u_viewProj[" + std::to_string(layer) + "]";
        m_shadow.setUniformMatrix4fv(name.c_str(), mapView.viewProj[m_dir + layer]);
    }
    if (planned.kind != LightKind::Sky) return;
    m_shadow.setUniform1i("u_skyLayers", m_pass);
    for (int layer = 0; layer < m_pass; ++layer) {
        const glm::vec3   up   = skyward(m_view * 6 + static_cast<uint32_t>(m_dir + layer));
        const std::string name = "u_skyward[" + std::to_string(layer) + "]";
        m_shadow.setUniform3f(name.c_str(), up.x, up.y, up.z);
    }
    // The heights sum to half the directions: a texel facing up, hidden everywhere, sees none.
    m_shadow.setUniform1f("u_skyShare", 2.0f / static_cast<float>(SKY_DIRECTIONS));
}

void GLLightmapBaker::rasterReceivers(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    const std::vector<uint32_t>& receivers
) {
    const auto side = static_cast<int32_t>(GLLightmaps::PAGE_SIZE);
    const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    m_pageTarget.bind();
    m_pageTarget.attachTexture(GL_COLOR_ATTACHMENT0, m_position[0]->getID());
    m_pageTarget.attachTexture(GL_COLOR_ATTACHMENT1, m_normal[0]->getID());
    m_pageTarget.setDrawBuffers(buffers, 2);
    gl.setViewport(0, 0, side, side);
    gl.setDepthTest(false);
    gl.setDepthWrite(false);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_pageTarget.clearColor(0, none);
    m_pageTarget.clearColor(1, none);

    objects.bind();
    m_raster.bind();
    // The meshes they were given, as their places on the pages were sized from.
    const auto& drawn = m_batcher.buildGrouped(receivers, *view.objects, mirror, 0, MeshOf::Base);
    for (const InstanceDraw& draw : drawn) m_batcher.draw(gl, draw);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

void GLLightmapBaker::spread(int pass) {
    // A texel a pass, ending where it began.
    static_assert(DILATE_PASSES % 2 == 0, "the spread ends in the textures it began from");
    const int from = pass % 2;
    const int to   = 1 - from;
    m_dilate.bind();
    m_position[from]->bindSlot(GLBindings::LightmapBakeSlots::POSITION);
    m_normal[from]->bindSlot(GLBindings::LightmapBakeSlots::NORMAL);
    m_position[to]->bindImage(0, GL_WRITE_ONLY);
    m_normal[to]->bindImage(1, GL_WRITE_ONLY);
    namespace Groups = GLBindings::ComputeGroups;
    const uint32_t groups = Groups::covering(GLLightmaps::PAGE_SIZE, Groups::IMAGE);
    m_dilate.dispatch(groups, groups);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

bool GLLightmapBaker::ensureMaps(uint32_t side, int layers, GLenum format) {
    const auto count = static_cast<uint32_t>(layers);
    if (m_maps && m_maps->getWidth() == side && m_maps->layers() == count
        && m_maps->getParams().internalFormat == format) {
        return false;
    }
    // Detached first, as releaseScratch() says.
    m_mapTarget.attachTexture(GL_DEPTH_ATTACHMENT, 0);
    // Still in the frames in flight: freed once they are done.
    m_spentMaps = std::move(m_maps);
    m_spentIn   = GLLightmaps::RETIRE_FRAMES;
    Vkm::OpenGL::Texture2DArrayParams params;
    params.width          = side;
    params.height         = side;
    params.layers         = count;
    params.internalFormat = format;
    params.minFilter      = Vkm::OpenGL::TextureMinFilter::Nearest;
    params.magFilter      = Vkm::OpenGL::TextureMagFilter::Nearest;
    m_maps                = std::make_unique<Vkm::OpenGL::Texture2DArray>("lightmap_bake_maps", params);
    return true;
}

} // namespace Vkm::Engine
