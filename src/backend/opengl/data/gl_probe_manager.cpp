#include "data/gl_probe_manager.h"

#include <algorithm>

#include <GL/glew.h>

#include "data/gl_probe.h"
#include "data/gl_probe_baker.h"
#include "convention/gl_bindings.h"
#include "gl_uniform_buffer.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
// Matches ProbeBlock in shaders/forward/pbr (std140). One entry per probe.
struct GpuProbe {
    glm::vec4 center;    ///< xyz world centre, w pad
    glm::vec4 extents;   ///< xyz half-extents, w pad
    glm::vec4 params;    ///< x falloff, y intensity, z layer index, w pad
};
struct ProbeBlock {
    GpuProbe probes[GLBindings::ProbeTextureSlots::MAX_PROBES];
};
}

GLProbeManager::GLProbeManager() = default;
GLProbeManager::~GLProbeManager() = default;

void GLProbeManager::init() {
    // Both compile shaders, so this must run with a live GL context.
    m_baker = std::make_unique<GLProbeBaker>();
    m_array = std::make_unique<GLProbeArray>();
    m_array->createTargets(static_cast<int>(GLBindings::ProbeTextureSlots::MAX_PROBES));
}

int GLProbeManager::bind(const RenderView& view) {
    if (!m_array) return 0;

    // Collect the baked probes within array capacity, with their camera distance.
    struct Active { uint32_t index; float dist; };
    std::vector<Active> active;
    const int capacity = m_array->capacity();
    for (size_t i = 0; i < view.probes.size() && static_cast<int>(i) < capacity; ++i) {
        if (i >= m_state.size() || !m_state[i].baked) continue;
        active.push_back({ static_cast<uint32_t>(i),
            glm::distance(view.camera.position, view.probes[i].position) });
    }

    // Keep the nearest MAX_PROBES (the shader's per-fragment blend loop bound).
    const size_t maxP = GLBindings::ProbeTextureSlots::MAX_PROBES;
    if (active.size() > maxP) {
        std::partial_sort(active.begin(), active.begin() + maxP, active.end(),
            [](const Active& a, const Active& b) { return a.dist < b.dist; });
        active.resize(maxP);
    }

    // Pack the ProbeBlock UBO; params.z carries the cube-array layer index.
    ProbeBlock block{};
    for (size_t p = 0; p < active.size(); ++p) {
        const ProbeData& pd = view.probes[active[p].index];
        block.probes[p].center  = glm::vec4(pd.position, 0.0f);
        block.probes[p].extents = glm::vec4(pd.halfExtents, 0.0f);
        block.probes[p].params  = glm::vec4(pd.falloff, pd.intensity, static_cast<float>(active[p].index), 0.0f);
    }
    if (!m_ubo) m_ubo = std::make_unique<Core::UniformBuffer>(&block, sizeof(block), GL_DYNAMIC_DRAW);
    else        m_ubo->update(&block, sizeof(block));
    m_ubo->bindBase(GLBindings::UBOBindingPoints::Probes);

    m_array->bindIrradiance(GLBindings::ProbeTextureSlots::Irradiance);
    m_array->bindPrefilter(GLBindings::ProbeTextureSlots::Prefilter);
    return static_cast<int>(active.size());
}

void GLProbeManager::update(Core::Context& gl, const RenderView& view, const GLView& glView, const GLIBL& ibl) {
    if (!m_baker || !m_array) return;

    // Re-bake probes that are new, moved, or version-bumped. Throttled so several
    // changing at once don't hitch the frame.
    constexpr int MAX_REBAKES_PER_FRAME = 1;
    const int capacity = m_array->capacity();
    const int n = std::min(static_cast<int>(view.probes.size()), capacity);
    if (static_cast<int>(m_state.size()) < n) m_state.resize(n);

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

} // namespace Engine
