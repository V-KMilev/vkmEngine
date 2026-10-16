#include "storage/gl_probe_manager.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "gl_buffer_upload.h"
#include "gl_uniform_buffer.h"

#include "gl_profiler.h"
#include "convention/gl_bindings.h"
#include "offline/gl_probe_baker.h"
#include "offline/gl_scene_capture.h"
#include "storage/gl_irradiance_volume.h"
#include "storage/gl_probe_array.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLProbeManager::GLProbeManager() = default;
GLProbeManager::~GLProbeManager() = default;

void GLProbeManager::init(GLSceneCapture& capture, GLCubeConvolver& convolver) {
    m_baker = std::make_unique<GLProbeBaker>(capture, convolver);
    m_array = std::make_unique<GLProbeArray>();
    m_array->createTargets(
        static_cast<int>(GLBindings::ProbeTextureSlots::MAX_PROBES),
        GLProbeArray::DEFAULT_RESOLUTION
    );
}

void GLProbeManager::assignLayers(const RenderView& view) {
    const size_t capacity = static_cast<size_t>(m_array->capacity());
    m_state.resize(capacity);
    m_seen.assign(capacity, 0);
    m_layerOf.assign(view.probes.size(), -1);

    // Probes that already hold a layer keep it.
    for (size_t i = 0; i < view.probes.size(); ++i) {
        for (size_t layer = 0; layer < capacity; ++layer) {
            if (!m_state[layer].owned || m_state[layer].owner != view.probes[i].entitySlot) continue;
            m_layerOf[i]  = static_cast<int>(layer);
            m_seen[layer] = 1;
            break;
        }
    }

    // A layer whose probe is gone holds a capture of nothing in the scene.
    for (size_t layer = 0; layer < capacity; ++layer) {
        if (!m_seen[layer]) m_state[layer] = BakeState{};
    }

    // New probes take the free layers, unbaked, in list order.
    size_t nextFree = 0;
    for (size_t i = 0; i < view.probes.size(); ++i) {
        if (m_layerOf[i] >= 0) continue;
        while (nextFree < capacity && m_state[nextFree].owned) ++nextFree;
        if (nextFree == capacity) break;
        m_state[nextFree].owned = true;
        m_state[nextFree].owner = view.probes[i].entitySlot;
        m_layerOf[i] = static_cast<int>(nextFree);
    }
}

int GLProbeManager::bind(const RenderView& view) {
    if (!m_array) return 0;

    // Baked probes, each at its entity's layer, smallest box first (the shader's blend order),
    // whatever their scene order. MAX_PROBES layers, the same bound as the shader's blend loop.
    assignLayers(view);
    m_active.clear();
    for (size_t i = 0; i < view.probes.size(); ++i) {
        const int layer = m_layerOf[i];
        if (layer < 0 || !m_state[static_cast<size_t>(layer)].baked) continue;
        m_active.push_back(static_cast<uint32_t>(i));
    }
    const auto volume = [&](uint32_t i) {
        const glm::vec3 h = view.probes[i].halfExtents;
        return h.x * h.y * h.z;
    };
    std::sort(m_active.begin(), m_active.end(), [&](uint32_t a, uint32_t b) {
        if (volume(a) != volume(b)) return volume(a) < volume(b);
        return view.probes[a].entitySlot < view.probes[b].entitySlot;
    });

    // params.z carries the cube-array layer. The upload is skipped when the block matches last
    // frame's, so a still scene costs no GPU write.
    ProbeBlock block{};
    for (size_t p = 0; p < m_active.size(); ++p) {
        const ProbeData& pd = view.probes[m_active[p]];
        block.probes[p].center  = glm::vec4(pd.position, 0.0f);
        block.probes[p].extents = glm::vec4(pd.halfExtents, 0.0f);
        block.probes[p].params  = glm::vec4(
            pd.falloff,
            pd.intensity,
            static_cast<float>(m_layerOf[m_active[p]]),
            0.0f
        );
    }
    Vkm::GL::uploadIfChanged(m_ubo, m_lastBlock, block);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::PROBES);

    m_array->bindIrradiance(GLBindings::ProbeTextureSlots::IRRADIANCE);
    m_array->bindPrefilter(GLBindings::ProbeTextureSlots::PREFILTER);
    return static_cast<int>(m_active.size());
}

void GLProbeManager::update(
    Vkm::GL::Context& gl,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const GLIBL& ibl,
    const GLIrradianceVolume* volume,
    const CaptureLighting& lighting
) {
    if (!m_baker || !m_array) return;
    ++m_frame;
    m_seconds += view.frameSeconds;

    // The layers bind() assigned this frame, from this same view.
    const size_t n = std::min(view.probes.size(), m_layerOf.size());

    // Array textures force one face size across every layer, so the shared arrays take
    // the highest resolution any active probe asks for; changing it drops every bake.
    int  desired = GLProbeArray::MIN_RESOLUTION;
    bool any     = false;
    for (size_t i = 0; i < n; ++i) {
        if (m_layerOf[i] < 0) continue;
        desired = std::max(desired, static_cast<int>(view.probes[i].resolution));
        any     = true;
    }
    desired = GLProbeArray::clampResolution(desired);
    if (any && desired != m_array->resolution()) {
        m_array->createTargets(m_array->capacity(), desired);
        for (BakeState& st : m_state) st.baked = false;
        m_baker->endRefresh();
    }

    // How long each probe has held its place and box, every frame, whichever bakes below; and the
    // lights that reach it, within half its width of its box: a lamp moving across the world does
    // not keep every probe refreshing.
    m_lighting.assign(n, lighting);
    for (size_t i = 0; i < n; ++i) {
        if (m_layerOf[i] < 0) continue;
        const ProbeData& pd = view.probes[i];
        m_state[static_cast<size_t>(m_layerOf[i])].seen.see({pd.position, pd.halfExtents});
        const float      half  = glm::max(pd.halfExtents.x, glm::max(pd.halfExtents.y, pd.halfExtents.z));
        const glm::vec3  reach = pd.halfExtents + glm::vec3(half);
        const Math::AABB within{pd.position - reach, pd.position + reach};
        m_lighting[i].lights = lightingDigest(view.lights, &within);
    }

    // Throttled so several probes changing at once don't hitch the frame.
    constexpr int MAX_REBAKES_PER_FRAME = 1;
    const uint32_t volumeBake = volume ? volume->bakeId() : 0;
    int rebakes = 0;
    for (size_t i = 0; i < n && rebakes < MAX_REBAKES_PER_FRAME; ++i) {
        const int layer = m_layerOf[i];
        if (layer < 0) continue;
        const ProbeData& pd = view.probes[i];
        BakeState&       st = m_state[static_cast<size_t>(layer)];
        const Placement here{pd.position, pd.halfExtents};
        const bool      moved  = st.placed != here;
        const bool      forced = st.version != pd.bakeVersion;
        const bool      relit  = st.volumeBake != volumeBake;

        // A probe being moved or resized bakes once it holds still; a new version is a request
        // to bake now, and a new lighting or a first bake does not wait either.
        if (st.baked && !forced && !relit && !(moved && st.seen.settled())) continue;
        m_baker->bake(gl, *m_array, layer, pd, view, glView, resources, ibl, volume);
        st.baked      = true;
        st.placed     = here;
        st.version    = pd.bakeVersion;
        st.volumeBake = volumeBake;
        st.lighting   = m_lighting[i];
        st.captured   = m_frame;
        ++rebakes;
    }

    // A bake is the frame's capture, and ended any refresh (GLProbeBaker::bake).
    if (rebakes > 0) return;
    refresh(gl, view, glView, resources, ibl, volume);
}

void GLProbeManager::refresh(
    Vkm::GL::Context& gl,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const GLIBL& ibl,
    const GLIrradianceVolume* volume
) {
    const size_t n = std::min(view.probes.size(), m_layerOf.size());

    // Where it was captured: one away from there - riding a platform, being dragged - is not
    // refreshed, or its faces would come from as many places.
    const auto inPlace = [&](size_t i, const BakeState& st) {
        return st.placed == Placement{view.probes[i].position, view.probes[i].halfExtents};
    };

    // The probe whose refresh is under way, while it still holds its layer and its place.
    size_t probe = n;
    if (m_refreshLayer >= 0) {
        for (size_t i = 0; i < n; ++i) {
            if (m_layerOf[i] == m_refreshLayer) probe = i;
        }
        const bool keep = probe < n && m_baker->refreshing()
            && inPlace(probe, m_state[static_cast<size_t>(m_refreshLayer)]);
        if (!keep) {
            m_baker->endRefresh();
            m_refreshLayer = -1;
        }
    }

    // Else the one captured under other light that has waited longest for how near the eye it is
    // (REFRESH_NEAR), of those not resting from their last refresh (REFRESH_REST).
    if (m_refreshLayer < 0) {
        float best = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const int layer = m_layerOf[i];
            if (layer < 0) continue;
            const BakeState& st = m_state[static_cast<size_t>(layer)];
            if (!st.baked || st.lighting == m_lighting[i] || !inPlace(i, st)) continue;
            if (m_seconds - st.refreshed < REFRESH_REST) continue;
            const float waited   = static_cast<float>(m_frame - st.captured);
            const float distance = glm::distance(view.camera.position, view.probes[i].position);
            const float score    = waited / (1.0f + distance / REFRESH_NEAR);
            if (score > best) {
                best  = score;
                probe = i;
            }
        }
        if (probe == n) return;
        m_refreshLayer    = m_layerOf[probe];
        m_refreshLighting = m_lighting[probe];
        m_baker->beginRefresh(*m_array);
    }

    PROFILE_SCOPE("Render/ProbeRefresh");
    PROFILE_GPU_SCOPE("Render/ProbeRefresh");
    const ProbeData& pd = view.probes[probe];
    if (!m_baker->advance(gl, *m_array, m_refreshLayer, pd, view, glView, resources, ibl, volume)) return;

    // Under the light it began under: one that changed since takes another refresh.
    BakeState& refreshed = m_state[static_cast<size_t>(m_refreshLayer)];
    refreshed.lighting  = m_refreshLighting;
    refreshed.captured  = m_frame;
    refreshed.refreshed = m_seconds;
    m_refreshLayer = -1;
}

void GLProbeManager::invalidate() {
    for (BakeState& st : m_state) st.baked = false;
    if (m_baker) m_baker->endRefresh();
    m_refreshLayer = -1;
}

} // namespace Vkm::Engine
