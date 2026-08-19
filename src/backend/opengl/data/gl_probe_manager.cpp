#include "data/gl_probe_manager.h"

#include <algorithm>

#include <GL/glew.h>

#include "data/gl_probe.h"
#include "data/gl_probe_baker.h"
#include "convention/gl_bindings.h"
#include "gl_uniform_buffer.h"
#include "gl_buffer_upload.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLProbeManager::GLProbeManager() = default;
GLProbeManager::~GLProbeManager() = default;

void GLProbeManager::init(GLSceneCapture& capture, GLCubeConvolver& convolver) {
    // createTargets allocates cube-map arrays, so this must run with a live GL context.
    m_baker = std::make_unique<GLProbeBaker>(capture, convolver);
    m_array = std::make_unique<GLProbeArray>();
    m_array->createTargets(static_cast<int>(GLBindings::ProbeTextureSlots::MAX_PROBES),
                           GLProbeArray::DEFAULT_RESOLUTION);
}

int GLProbeManager::bind(const RenderView& view) {
    if (!m_array) return 0;

    // Collect the baked probes, in scene order. A probe's layer in the cube
    // arrays is its scene index, and the arrays hold MAX_PROBES layers - the
    // same bound as the shader's per-fragment blend loop - so anything past the
    // capacity has nowhere to have been baked and nothing to be bound into.
    m_active.clear();
    const int capacity = m_array->capacity();
    for (size_t i = 0; i < view.probes.size() && static_cast<int>(i) < capacity; ++i) {
        if (i >= m_state.size() || !m_state[i].baked) continue;
        m_active.push_back(static_cast<uint32_t>(i));
    }

    // Pack the ProbeBlock UBO; params.z carries the cube-array layer index. The
    // upload is skipped when the packed block matches last frame (static scene +
    // still camera), so it costs no GPU write per frame.
    ProbeBlock block{};
    for (size_t p = 0; p < m_active.size(); ++p) {
        const ProbeData& pd = view.probes[m_active[p]];
        block.probes[p].center  = glm::vec4(pd.position, 0.0f);
        block.probes[p].extents = glm::vec4(pd.halfExtents, 0.0f);
        block.probes[p].params  = glm::vec4(pd.falloff, pd.intensity, static_cast<float>(m_active[p]), 0.0f);
    }
    Vkm::GL::uploadIfChanged(m_ubo, m_lastBlock, block);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::Probes);

    m_array->bindIrradiance(GLBindings::ProbeTextureSlots::Irradiance);
    m_array->bindPrefilter(GLBindings::ProbeTextureSlots::Prefilter);
    return static_cast<int>(m_active.size());
}

void GLProbeManager::update(Vkm::GL::Context& gl, const RenderView& view, const GLView& glView, const GLIBL& ibl) {
    if (!m_baker || !m_array) return;

    const int capacity = m_array->capacity();
    const int n = std::min(static_cast<int>(view.probes.size()), capacity);
    if (static_cast<int>(m_state.size()) < n) m_state.resize(n);

    // Size the shared cube arrays to the highest resolution any active probe
    // requests - array textures force one face size across every layer, so a
    // per-probe request drives the shared build via its maximum. On a change,
    // rebuild the arrays and force every probe to re-bake into the new size.
    if (n > 0) {
        int desired = GLProbeArray::MIN_RESOLUTION;
        for (int i = 0; i < n; ++i)
            desired = std::max(desired, static_cast<int>(view.probes[i].resolution));
        desired = GLProbeArray::clampResolution(desired);
        if (desired != m_array->resolution()) {
            m_array->createTargets(capacity, desired);
            for (BakeState& st : m_state) st.baked = false;
        }
    }

    // Throttled so several probes changing at once don't hitch the frame.
    constexpr int MAX_REBAKES_PER_FRAME = 1;
    int rebakes = 0;
    for (int i = 0; i < n && rebakes < MAX_REBAKES_PER_FRAME; ++i) {
        const ProbeData& pd = view.probes[i];
        BakeState&       st = m_state[i];
        const bool moved  = glm::distance(st.position, pd.position) > 1e-3f;
        const bool forced = st.version != pd.bakeVersion;
        if (st.baked && !moved && !forced) continue;
        m_baker->bake(gl, *m_array, i, pd.position, view, glView, ibl);
        st.baked    = true;
        st.position = pd.position;
        st.version  = pd.bakeVersion;
        ++rebakes;
    }
}

void GLProbeManager::invalidate() {
    for (BakeState& st : m_state) st.baked = false;
}

} // namespace Vkm::Engine
