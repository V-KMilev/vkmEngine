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

#include "gl_context.h"
#include "logger.h"

#include "gl_view.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "core/fnv1a.h"
#include "core/math/bounds.h"
#include "debug/profiler.h"
#include "frame/gl_lights.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_shadow_data.h"
#include "offline/gl_cubemap.h"
#include "storage/gl_lightmaps.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// A local light's maps start this near it, metres.
constexpr float MAP_NEAR = 0.05f;

// A point light's faces, each wider than a quarter turn so a filter disk near an edge stays on it.
const float POINT_FACE_FOV = glm::radians(100.0f);

// Metres the sun's map reaches past the receivers it covers, so a caster at their edge is in it.
constexpr float SUN_MARGIN = 2.0f;

// Whether object @p o of @p view takes a lightmap: keyed, fixed (ObjectDraw::fixed), the mesh it was
// given (RenderObjects::bases) unwrapped and its material Opaque.
bool receives(const RenderView& view, const GLView& glView, uint32_t o) {
    if (o >= view.objects->keys.size() || o >= view.objects->bases.size()) return false;
    const ObjectDraw& draw = view.objects->draws[o];
    if (!draw.fixed) return false;
    const GLMesh* mesh = glView.getMesh(view.objects->bases[o]);
    if (!mesh || !mesh->lightmapped() || mesh->layout() != VertexLayout::Static) return false;
    const GLMaterial* material = glView.getMaterial(draw.material);
    return material && material->getType() == MaterialType::Opaque;
}

// A 2D texture of @p format, @p side a side, read texel for texel.
std::unique_ptr<Vkm::GL::Texture2D> makeTarget(const char* name, GLenum format, uint32_t side) {
    const Vkm::GL::Texture2DParams params = Vkm::GL::renderTargetParams(side, side, format, GL_RGBA);
    return std::make_unique<Vkm::GL::Texture2D>(name, params);
}

} // namespace

GLLightmapBaker::GLLightmapBaker()
    : m_raster("shaders/lightmap/raster")
    , m_dilate("shaders/lightmap/dilate")
    , m_shadow("shaders/lightmap/shadow") {}

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
}

void GLLightmapBaker::invalidate() {
    releaseScratch();
    m_baking     = false;
    m_fixedKnown = false;
    m_baked.reset();
}

void GLLightmapBaker::update(
    Vkm::GL::Context& gl,
    const RenderView& view,
    const GLView& glView,
    const GLObjectBuffer& objects,
    GLLightmaps& lightmaps
) {
    if (!view.objects) return;
    const uint64_t ask = askOf(view, glView);
    m_settle.see(ask);
    // A change under way is baked over once it settles: what was planned is of another world.
    if (m_baking && ask != m_bakingAsk) {
        lightmaps.dropBake();
        releaseScratch();
        m_baking = false;
    }
    if (!m_baking && m_baked != ask && m_settle.settled()) {
        m_bakingAsk = ask;
        if (!plan(view, glView, lightmaps)) {
            m_baked = ask;
            return;
        }
    }
    if (m_baking) slice(gl, view, glView, objects, lightmaps, view.bakeBudget > 0.0f);
}

RenderReadiness GLLightmapBaker::progress() const {
    if (m_baking) return {m_stepsDone, m_steps};
    return pending() ? RenderReadiness{0, 1} : RenderReadiness{};
}

const std::vector<uint32_t>& GLLightmapBaker::resolve(const std::vector<uint64_t>& keys) {
    m_resolved.clear();
    for (const uint64_t key : keys) {
        const auto found = m_indexOf.find(key);
        if (found != m_indexOf.end()) m_resolved.push_back(found->second);
    }
    return m_resolved;
}

uint64_t GLLightmapBaker::askOf(const RenderView& view, const GLView& glView) {
    // The fixed meshes' part is worked out again only when it may have changed: the cull moves
    // fixedSerial when a fixed mesh does, and the mirror its revision when an upload lands. A frame
    // where everything may differ is not held whatever the serial says: it is another cull's count.
    const RenderObjects& objects = *view.objects;
    const bool held = m_fixedKnown && !objects.allChanged && objects.fixedSerial == m_fixedSerial
        && glView.syncedRevision() == m_fixedRevision;
    if (!held) {
        m_fixedDigest   = fixedDigest(objects, glView);
        m_fixedSerial   = objects.fixedSerial;
        m_fixedRevision = glView.syncedRevision();
        m_fixedKnown    = true;
    }
    // Chained in the channels' order: a light taking another's channel is another bake.
    uint64_t lights = FNV1A_OFFSET_BASIS;
    GLLightmaps::bakedLights(view.lights, m_lights);
    for (const uint32_t i : m_lights) {
        const LightData& light = view.lights[i];
        const uint64_t   shape = GLLightmaps::shapeOf(light);
        lights = fnv1a64Bytes(&light.entitySlot, sizeof(light.entitySlot), lights);
        lights = fnv1a64Bytes(&shape, sizeof(shape), lights);
    }
    return fnv1a64Bytes(&lights, sizeof(lights), fnv1a64Bytes(&m_fixedDigest, sizeof(m_fixedDigest)));
}

uint64_t GLLightmapBaker::fixedDigest(const RenderObjects& objects, const GLView& glView) {
    // Summed, not chained: what a bake is made from does not change when an index or the order
    // does, as meshes come and go.
    uint64_t fixed = 0;
    for (size_t i = 0; i < objects.scene.size(); ++i) {
        const uint32_t    o    = objects.scene[i];
        const ObjectDraw& draw = objects.draws[o];
        if (!draw.fixed || o >= objects.keys.size() || o >= objects.bases.size()) continue;
        // Whether the mesh it was given has landed, and with lightmap coordinates, not which mesh:
        // the key and its shape name it. The bake draws it, not the level the eye picked, so a level
        // swapped in as the eye moves changes nothing baked; a caster not yet landed casts nothing.
        const GLMesh* mesh    = glView.getMesh(objects.bases[o]);
        const bool    landed  = mesh != nullptr;
        const bool    unwraps = landed && mesh->lightmapped();
        const bool    casts   = i < objects.casterCount;
        uint64_t one = fnv1a64Bytes(&objects.keys[o], sizeof(objects.keys[o]));
        one = fnv1a64Bytes(&landed, sizeof(landed), one);
        one = fnv1a64Bytes(&unwraps, sizeof(unwraps), one);
        one = fnv1a64Bytes(&casts, sizeof(casts), one);
        one = fnv1a64Bytes(&objects.models[o], sizeof(objects.models[o]), one);
        // Its shape and its surface as they are now: a sculpt, a re-import or a material made a
        // cutout keeps every handle.
        const uint64_t shape = o < objects.shapes.size() ? objects.shapes[o] : 0;
        one = fnv1a64Bytes(&shape, sizeof(shape), one);
        const GLMaterial* surface = glView.getMaterial(draw.material);
        const uint64_t    looks   = surface ? surface->uploadId() : 0;
        one = fnv1a64Bytes(&looks, sizeof(looks), one);
        fixed += fnv1a64Bytes(&draw.material, sizeof(draw.material), one);
    }
    return fixed;
}

bool GLLightmapBaker::plan(const RenderView& view, const GLView& glView, GLLightmaps& lightmaps) {
    PROFILE_SCOPE("Lightmap/Plan");
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    m_started = std::chrono::duration<double>(since).count();
    const RenderObjects& scene = *view.objects;
    const uint32_t       page  = GLLightmaps::PAGE_SIZE;

    // The receivers, the largest first, each a square sized by its surface in the world.
    std::vector<std::pair<uint32_t, uint32_t>> squares;
    for (const uint32_t o : scene.scene) {
        if (!receives(view, glView, o)) continue;
        // Each axis's share by how far the model stretches the plane across it: a cofactor column.
        const glm::mat3 model(scene.models[o]);
        const glm::vec3 stretch(
            glm::length(glm::cross(model[1], model[2])),
            glm::length(glm::cross(model[2], model[0])),
            glm::length(glm::cross(model[0], model[1]))
        );
        const float    area = glm::dot(glView.getMesh(scene.bases[o])->lightmapArea(), stretch);
        const float    side = std::sqrt(area) * TEXELS_PER_METRE;
        const uint32_t even = (static_cast<uint32_t>(std::ceil(side)) + 3u) & ~3u;
        squares.emplace_back(o, std::clamp(even, MIN_SIDE, MAX_SIDE));
    }
    const auto larger = [](const auto& a, const auto& b) { return a.second > b.second; };
    std::sort(squares.begin(), squares.end(), larger);

    // The lights baked, a channel each.
    std::vector<GLLightmaps::BakedLight> channels;
    std::vector<const LightData*>        lights;
    GLLightmaps::bakedLights(view.lights, m_lights);
    for (const uint32_t i : m_lights) {
        const LightData& light = view.lights[i];
        channels.push_back({light.entitySlot, static_cast<int>(lights.size()), GLLightmaps::shapeOf(light)});
        lights.push_back(&light);
    }
    if (squares.empty() || lights.empty()) {
        lightmaps.beginBake(scene, glView, {}, 0, {});
        lightmaps.finishBake();
        return false;
    }

    // Shelves across the pages, a row the height of its first, tallest square.
    GLLightmaps::Places places;
    m_pageKeys.assign(1, {});
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t row = 0;
    for (const auto& [o, side] : squares) {
        if (x + side > page) {
            x = 0;
            y += row;
            row = 0;
        }
        if (y + side > page) {
            m_pageKeys.emplace_back();
            x = 0;
            y = 0;
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
    lightmaps.beginBake(scene, glView, std::move(places), pageCount, std::move(channels));

    // What casts: fixed, and in the casting prefix of the scene list.
    const float most = std::numeric_limits<float>::max();
    Math::AABB  receiverBounds{glm::vec3(most), glm::vec3(-most)};
    for (const auto& [o, side] : squares) {
        receiverBounds.min = glm::min(receiverBounds.min, scene.bounds[o].min);
        receiverBounds.max = glm::max(receiverBounds.max, scene.bounds[o].max);
    }
    std::vector<uint32_t> casters;
    for (uint32_t i = 0; i < scene.casterCount && i < scene.scene.size(); ++i) {
        const uint32_t o = scene.scene[i];
        if (scene.draws[o].fixed) casters.push_back(o);
    }

    m_plan.clear();
    for (const LightData* baked : lights) {
        const LightData& light = *baked;
        LightKind kind       = LightKind::Sun;
        int       layers     = 1;
        float     source     = 0.0f;
        float     zNear      = MAP_NEAR;
        float     texelWorld = 0.0f;
        uint32_t  side       = LOCAL_MAP;

        // The views the light's maps are drawn through: a local light's one, the sun's tiles.
        std::vector<MapView> views;
        if (light.type == LightType::Directional) {
            kind       = LightKind::Sun;
            side       = SUN_MAP;
            source     = std::tan(light.sourceRadius);
            texelWorld = SUN_TEXEL;
            const glm::vec3  dir       = glm::normalize(light.direction);
            const glm::mat4  lightView = glm::lookAt(glm::vec3(0.0f), dir, stableUp(dir));
            const Math::AABB across    = Math::transform(lightView, receiverBounds);
            const glm::vec2  lo        = glm::vec2(across.min) - glm::vec2(SUN_MARGIN);
            const glm::vec2  hi        = glm::vec2(across.max) + glm::vec2(SUN_MARGIN);
            // A tile bakes its core; the rest of its map is the reach of the core's texels.
            const float reach = static_cast<float>(REACH_TEXELS) * SUN_TEXEL;
            const float core  = static_cast<float>(SUN_MAP - 2u * REACH_TEXELS) * SUN_TEXEL;
            const glm::ivec2 tiles = glm::max(glm::ivec2(glm::ceil((hi - lo) / core)), glm::ivec2(1));
            for (int ty = 0; ty < tiles.y; ++ty) {
                for (int tx = 0; tx < tiles.x; ++tx) {
                    const glm::vec2 from = lo + glm::vec2(tx, ty) * core - glm::vec2(reach);
                    const glm::vec2 to   = from + glm::vec2(core + 2.0f * reach);
                    MapView& tile = views.emplace_back();
                    float    top  = -std::numeric_limits<float>::max();
                    float    base = std::numeric_limits<float>::max();
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
                const Math::AABB& b = scene.bounds[o];
                const glm::vec3 nearest = glm::clamp(light.position, b.min, b.max);
                if (glm::distance(nearest, light.position) <= reach) only.casters.push_back(scene.keys[o]);
            }
            source = light.sourceRadius;
            // A spot too wide for one map bakes six faces, as a point light does.
            if (!castsThroughCube(light)) {
                kind = LightKind::Spot;
                const glm::vec3 dir  = glm::normalize(light.direction);
                const float     fov  = spotMapFov(light);
                const glm::mat4 v    = glm::lookAt(light.position, light.position + dir, stableUp(dir));
                const glm::mat4 p    = glm::perspective(fov, 1.0f, zNear, only.depth);
                only.viewProj[0] = GLDepth::shadowProjection(p * v);
                texelWorld       = 2.0f * std::tan(0.5f * fov) / static_cast<float>(side);
            } else {
                kind   = LightKind::Point;
                layers = 6;
                const glm::mat4 projection = glm::perspective(POINT_FACE_FOV, 1.0f, zNear, only.depth);
                for (int f = 0; f < 6; ++f) {
                    const glm::mat4 v = GLCubemap::faceView(f, light.position);
                    only.viewProj[f]  = GLDepth::shadowProjection(projection * v);
                }
                texelWorld = 2.0f * std::tan(0.5f * POINT_FACE_FOV) / static_cast<float>(side);
            }
        }

        ChannelPlan& planned = m_plan.emplace_back();
        planned.light      = light;
        planned.kind       = kind;
        planned.layers     = layers;
        planned.side       = side;
        planned.source     = source;
        planned.zNear      = zNear;
        planned.texelWorld = texelWorld;
        planned.views      = std::move(views);
    }

    m_steps = 0;
    for (const ChannelPlan& planned : m_plan) {
        m_steps += static_cast<uint32_t>(planned.views.size()) * pageCount;
    }
    m_receivers = static_cast<uint32_t>(squares.size());
    m_channel   = 0;
    m_view      = 0;
    m_page      = 0;
    m_stepsDone = 0;
    m_frames    = 0;
    m_mapsHold  = -1;
    m_pageHold  = -1;
    m_baking    = true;
    return true;
}

void GLLightmapBaker::slice(
    Vkm::GL::Context& gl,
    const RenderView& view,
    const GLView& glView,
    const GLObjectBuffer& objects,
    GLLightmaps& lightmaps,
    bool sliced
) {
    PROFILE_SCOPE("Lightmap/Bake");
    using Wall = std::chrono::steady_clock;
    const uint32_t page = GLLightmaps::PAGE_SIZE;
    if (!m_position[0]) {
        for (int i = 0; i < 2; ++i) {
            m_position[i] = makeTarget("lightmap_bake_position", GL_RGBA32F, page);
            m_normal[i]   = makeTarget("lightmap_bake_normal", GL_RGBA16F, page);
        }
    }
    // The plan names objects by key: this frame's index of each.
    const RenderObjects& scene = *view.objects;
    m_indexOf.clear();
    for (uint32_t o = 0; o < scene.keys.size(); ++o) m_indexOf.emplace(scene.keys[o], o);
    ++m_frames;

    // A light none of whose views casts anything has no steps: it is passed over.
    while (m_stepsDone < m_steps) {
        const ChannelPlan& planned = m_plan[m_channel];
        if (m_view >= planned.views.size()) {
            ++m_channel;
            m_view = 0;
            continue;
        }
        const MapView& mapView = planned.views[m_view];

        const int64_t hold = (static_cast<int64_t>(m_channel) << 32) | m_view;
        if (m_mapsHold != hold) {
            ensureMaps(planned.side, planned.layers);
            const std::vector<uint32_t>& casters = resolve(mapView.casters);
            for (int layer = 0; layer < planned.layers; ++layer) {
                m_mapTarget.bind();
                m_mapTarget.attachLayer(GL_DEPTH_ATTACHMENT, m_maps->getID(), 0, layer);
                m_mapTarget.setDrawBuffer(GL_NONE);
                m_mapTarget.setReadBuffer(GL_NONE);
                const glm::mat4& lightVP = mapView.viewProj[layer];
                m_casters.draw(gl, casters, scene, glView, objects, lightVP, planned.side, MeshOf::Base);
            }
            m_mapsHold = hold;
        }
        if (m_pageHold != m_page) {
            lightmaps.bindBakingObjects(scene);
            drawReceivers(gl, view, glView, objects, resolve(m_pageKeys[m_page]));
            m_pageHold = m_page;
        }

        const LightData& light = planned.light;
        m_shadow.bind();
        m_shadow.setUniform1i("u_page", static_cast<int>(m_page));
        m_shadow.setUniform1i("u_channel", static_cast<int>(m_channel));
        m_shadow.setUniform1i("u_kind", static_cast<int>(planned.kind));
        for (int layer = 0; layer < 6; ++layer) {
            const std::string name = "u_viewProj[" + std::to_string(layer) + "]";
            m_shadow.setUniformMatrix4fv(name.c_str(), mapView.viewProj[layer]);
        }
        m_shadow.setUniform3f("u_lightPos", light.position.x, light.position.y, light.position.z);
        const glm::vec3 dir = glm::normalize(light.direction);
        m_shadow.setUniform3f("u_lightDir", dir.x, dir.y, dir.z);
        m_shadow.setUniform1f("u_source", planned.source);
        m_shadow.setUniform1f("u_near", planned.zNear);
        m_shadow.setUniform1f("u_far", mapView.depth);
        m_shadow.setUniform1f("u_texelWorld", planned.texelWorld);
        m_shadow.setUniform1f("u_reach", static_cast<float>(REACH_TEXELS));
        m_shadow.setUniform1f("u_rim", mapView.rim);
        m_position[0]->bindSlot(GLBindings::LightmapBakeSlots::POSITION);
        m_normal[0]->bindSlot(GLBindings::LightmapBakeSlots::NORMAL);
        m_maps->bindSlot(GLBindings::LightmapBakeSlots::DEPTH);
        lightmaps.bakingPages().bindImage(0, GL_READ_WRITE);
        namespace Groups = GLBindings::ComputeGroups;
        const uint32_t groups = Groups::covering(page, Groups::IMAGE);
        m_shadow.dispatch(groups, groups);
        Vkm::GL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        ++m_stepsDone;
        if (++m_page == static_cast<uint32_t>(m_pageKeys.size())) {
            m_page = 0;
            ++m_view;
        }
        // A step is the GPU's work, not the CPU's: one a frame, which a clock on the CPU would not bound.
        if (sliced) break;
    }
    if (m_stepsDone < m_steps) return;

    lightmaps.finishBake();
    releaseScratch();
    m_baking = false;
    m_baked  = m_bakingAsk;
    const double now = std::chrono::duration<double>(Wall::now().time_since_epoch()).count();
    LOG_INFO(
        "Lightmaps baked: %u receiver(s) on %zu page(s), %zu fixed light(s), in %.0f ms over %u frame(s)",
        m_receivers,
        m_pageKeys.size(),
        m_plan.size(),
        (now - m_started) * 1000.0,
        m_frames
    );
}

void GLLightmapBaker::drawReceivers(
    Vkm::GL::Context& gl,
    const RenderView& view,
    const GLView& glView,
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
    const auto& drawn = m_batcher.buildGrouped(receivers, *view.objects, glView, 0, MeshOf::Base);
    for (const InstanceDraw& draw : drawn) m_batcher.draw(gl, draw);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    // Spread past the charts, a texel a pass, ending where it began.
    m_dilate.bind();
    namespace Groups = GLBindings::ComputeGroups;
    const uint32_t groups = Groups::covering(GLLightmaps::PAGE_SIZE, Groups::IMAGE);
    for (int pass = 0; pass < DILATE_PASSES; ++pass) {
        const int from = pass % 2;
        const int to   = 1 - from;
        m_position[from]->bindSlot(GLBindings::LightmapBakeSlots::POSITION);
        m_normal[from]->bindSlot(GLBindings::LightmapBakeSlots::NORMAL);
        m_position[to]->bindImage(0, GL_WRITE_ONLY);
        m_normal[to]->bindImage(1, GL_WRITE_ONLY);
        m_dilate.dispatch(groups, groups);
        Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
    static_assert(DILATE_PASSES % 2 == 0, "the spread ends in the textures it began from");
}

void GLLightmapBaker::ensureMaps(uint32_t side, int layers) {
    const auto count = static_cast<uint32_t>(layers);
    if (m_maps && m_maps->getWidth() == side && m_maps->layers() == count) return;
    // Detached first, as releaseScratch() says.
    m_mapTarget.attachTexture(GL_DEPTH_ATTACHMENT, 0);
    m_maps.reset();
    Vkm::GL::Texture2DArrayParams params;
    params.width          = side;
    params.height         = side;
    params.layers         = count;
    params.internalFormat = GL_DEPTH_COMPONENT32F;
    params.minFilter      = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter      = Vkm::GL::TextureMagFilter::Nearest;
    m_maps = std::make_unique<Vkm::GL::Texture2DArray>("lightmap_bake_maps", params);
}

} // namespace Vkm::Engine
