#include "storage/light/gl_lightmaps.h"

#include <algorithm>

#include <GL/glew.h>

#include "vkm_opengl/buffer/shader_storage_buffer.h"
#include "vkm_opengl/texture/texture_2d_array.h"

#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "base/hash/fnv1a.h"
#include "system/render/data/light_data.h"
#include "core/frame/render_objects.h"

namespace Vkm::Engine {

static_assert(
    sizeof(GLLightmaps::ObjectLightmap) == 32,
    "ObjectLightmap must match shaders/opengl/include/lightmap.glsl"
);

GLLightmaps::GLLightmaps() {
    makePages(m_shown, 0);
}

GLLightmaps::~GLLightmaps() = default;

void GLLightmaps::prepare(const RenderObjects& objects, const GLAssetMirror& mirror) {
    for (Retired& retired : m_retired) --retired.framesLeft;
    const auto spent = [](const Retired& retired) { return retired.framesLeft == 0; };
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), spent), m_retired.end());
    prepareSet(m_shown, objects, mirror);
    if (m_baking) prepareSet(*m_baking, objects, mirror);
}

void GLLightmaps::prepareSet(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror) {
    const size_t count = objects.draws.size();
    // The mirror walking every object again may have changed which meshes read which charts.
    const bool held = set.seen && count == set.objects.size() && mirror.syncedRevision() == set.revision;
    if (held && objects.serial == set.serial) {
        // The cull wrote nothing new.
    } else if (held && objects.serial == set.serial + 1 && !objects.allChanged) {
        // An index now holding another object is among those changed.
        for (const uint32_t o : objects.changed) set.dirty = place(set, objects, mirror, o) || set.dirty;
        set.serial = objects.serial;
    } else {
        placeAll(set, objects, mirror);
    }
    if (set.dirty || !set.buffer) {
        upload(set.objects, set.buffer);
        set.dirty = false;
    }
}

void GLLightmaps::beginBake(
    const RenderObjects& objects,
    const GLAssetMirror& mirror,
    Places places,
    uint32_t pageCount,
    std::vector<BakedLight> lightChannels
) {
    m_baking                = std::make_unique<Set>();
    m_baking->places        = std::move(places);
    m_baking->lightChannels = std::move(lightChannels);
    m_baking->pageCount     = pageCount;
    makePages(*m_baking, pageCount);
    placeAll(*m_baking, objects, mirror);
    upload(m_baking->objects, m_baking->buffer);
    m_baking->dirty = false;
}

void GLLightmaps::beginPartialBake(
    const RenderObjects& objects,
    const GLAssetMirror& mirror,
    std::vector<BakedLight> lightChannels
) {
    beginBake(objects, mirror, m_shown.places, m_shown.pageCount, std::move(lightChannels));
}

bool GLLightmaps::holdsChannel(uint32_t slot, int channel, uint64_t shape) const {
    for (const BakedLight& baked : m_shown.lightChannels) {
        if (baked.channel == channel) return baked.slot == slot && baked.shape == shape;
    }
    return false;
}

void GLLightmaps::bindBakingObjects(const RenderObjects& objects) {
    // Ungated: a receiver drawing a level that reads none of its charts, or none yet, is still drawn
    // as its given mesh into its own square, never over the page.
    const Places& places = m_baking->places;
    m_planned.assign(objects.draws.size(), ObjectLightmap{});
    for (uint32_t o = 0; o < m_planned.size() && o < objects.keys.size(); ++o) {
        const auto found = places.find(objects.keys[o]);
        if (found != places.end()) m_planned[o] = found->second.lightmap;
    }
    upload(m_planned, m_plannedBuffer);
    m_plannedBuffer->bindBase(GLBindings::SSBOBindingPoints::INSTANCE_LIGHTMAP);
}

void GLLightmaps::finishBake() {
    if (m_baking->pageCount > 0) {
        m_baking->pages->generateMipmaps();
        m_baking->sky->generateMipmaps();
    }
    // The shown set is still in the frames in flight: freed some frames on.
    m_retired.push_back({std::make_unique<Set>(std::move(m_shown)), RETIRE_FRAMES});
    m_shown = std::move(*m_baking);
    m_baking.reset();
}

void GLLightmaps::dropBake() {
    m_retired.push_back({std::move(m_baking), RETIRE_FRAMES});
}

void GLLightmaps::setProbeSky(const std::vector<uint32_t>& slots, const std::vector<float>& sky) {
    m_probeSky.clear();
    for (size_t i = 0; i < slots.size() && i < sky.size(); ++i) m_probeSky[slots[i]] = sky[i];
}

float GLLightmaps::probeSky(uint32_t slot) const {
    const auto found = m_probeSky.find(slot);
    return found != m_probeSky.end() ? found->second : 0.0f;
}

void GLLightmaps::clear() {
    m_baking.reset();
    m_probeSky.clear();
    m_retired.clear();
    m_planned.clear();
    m_plannedBuffer.reset();
    m_shown = Set{};
    makePages(m_shown, 0);
}

bool GLLightmaps::place(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror, uint32_t o) {
    // A moved receiver reads the live shadows until its re-bake lands; one made movable, or drawing a
    // level whose lightmap coordinates land elsewhere in its square, for as long as it is.
    ObjectLightmap entry{};
    if (o < objects.keys.size() && o < objects.bases.size()) {
        const auto found = set.places.find(objects.keys[o]);
        const ObjectDraw& draw = objects.draws[o];
        if (found != set.places.end() && draw.fixed && found->second.model == objects.models[o]
            && mirror.readsChartsOf(draw.mesh, objects.bases[o])) {
            entry = found->second.lightmap;
        }
    }
    ObjectLightmap& held = set.objects[o];
    const bool changed   = held.page != entry.page || held.scaleOffset != entry.scaleOffset;
    held = entry;
    return changed;
}

void GLLightmaps::placeAll(Set& set, const RenderObjects& objects, const GLAssetMirror& mirror) {
    set.objects.resize(objects.draws.size());
    for (uint32_t o = 0; o < set.objects.size(); ++o) place(set, objects, mirror, o);
    set.dirty    = true;
    set.seen     = true;
    set.serial   = objects.serial;
    set.revision = mirror.syncedRevision();
}

void GLLightmaps::makePages(Set& set, uint32_t count) {
    Vkm::OpenGL::Texture2DArrayParams params;
    params.width          = count > 0 ? PAGE_SIZE : 1;
    params.height         = params.width;
    params.layers         = std::max(count, 1u);
    params.levels         = count > 0 ? LEVELS : 1;
    params.internalFormat = GL_RGBA8;
    params.minFilter      = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
    set.pages             = std::make_unique<Vkm::OpenGL::Texture2DArray>("lightmap_pages", params);
    // Lit until a bake writes it, and for good where none does: a texel no light was baked into
    // casts nothing, and one the sky was not baked for sees all of it.
    const uint8_t lit[4] = {255, 255, 255, 255};
    for (uint32_t level = 0; level < params.levels; ++level) {
        set.pages->clear(level, GL_RGBA, GL_UNSIGNED_BYTE, lit);
    }
    params.internalFormat = GL_R8;
    set.sky               = std::make_unique<Vkm::OpenGL::Texture2DArray>("lightmap_sky", params);
    for (uint32_t level = 0; level < params.levels; ++level) {
        set.sky->clear(level, GL_RED, GL_UNSIGNED_BYTE, lit);
    }
}

void GLLightmaps::upload(
    const std::vector<ObjectLightmap>& entries,
    std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer>& buffer
) {
    // Never empty: an SSBO of no bytes cannot be made, and the draw reads it by index.
    const ObjectLightmap none{};
    const void*    data  = entries.empty() ? static_cast<const void*>(&none) : entries.data();
    const size_t   held  = std::max<size_t>(entries.size(), 1);
    const uint32_t bytes = static_cast<uint32_t>(held * sizeof(ObjectLightmap));
    if (!buffer || buffer->getSize() < bytes) {
        buffer = std::make_unique<Vkm::OpenGL::ShaderStorageBuffer>(data, bytes);
    } else {
        buffer->update(data, bytes);
    }
}

void GLLightmaps::bindObjects() const {
    if (m_shown.buffer) m_shown.buffer->bindBase(GLBindings::SSBOBindingPoints::INSTANCE_LIGHTMAP);
}

void GLLightmaps::bindPages() const {
    m_shown.pages->bindSlot(GLBindings::LightmapTextureSlots::MASK);
    m_shown.sky->bindSlot(GLBindings::LightmapTextureSlots::SKY);
}

uint64_t GLLightmaps::shapeOf(const LightData& light) {
    uint64_t shape = FNV1A_OFFSET_BASIS;
    const auto mix = [&shape](const auto& field) { shape = fnv1a64Bytes(&field, sizeof(field), shape); };
    mix(light.type);
    mix(light.position);
    mix(light.direction);
    mix(light.radius);
    mix(light.outerConeAngle);
    mix(light.sourceRadius);
    return shape;
}

void GLLightmaps::bakedLights(const std::vector<LightData>& lights, std::vector<uint32_t>& baked) {
    baked.clear();
    for (uint32_t i = 0; i < lights.size(); ++i) {
        const LightData& light = lights[i];
        if (!light.movable && light.castShadows) baked.push_back(i);
    }
    std::sort(baked.begin(), baked.end(), [&lights](uint32_t a, uint32_t b) {
        return lights[a].entitySlot < lights[b].entitySlot;
    });
    if (baked.size() > static_cast<size_t>(MAX_CHANNELS)) baked.resize(MAX_CHANNELS);
}

LightSlots GLLightmaps::channels(const std::vector<LightData>& lights) {
    m_channels.assign(lights.size(), -1);
    bakedLights(lights, m_baked);
    for (const uint32_t i : m_baked) {
        const uint64_t shape = shapeOf(lights[i]);
        for (const BakedLight& baked : m_shown.lightChannels) {
            if (lights[i].entitySlot == baked.slot && shape == baked.shape) m_channels[i] = baked.channel;
        }
    }
    return {m_channels.data(), static_cast<uint32_t>(m_channels.size())};
}

} // namespace Vkm::Engine
