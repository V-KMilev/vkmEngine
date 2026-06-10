#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_instance_batcher.h"

#include <algorithm>

#include "gl_view.h"
#include "data/gl_mesh.h"
#include "system/render/data/drawable_data.h"

namespace Engine {

namespace {
// Instance attribute base locations - must match the layout(location=N) in the
// forward + prepass vertex shaders. A mat4 spans N..N+3.
constexpr uint32_t MODEL_ATTRIB  = 4;   // per-instance model       -> 4..7
constexpr uint32_t NORMAL_ATTRIB = 8;   // per-instance normal mat  -> 8..11
}

void GLInstanceBatcher::append(const DrawableData& d) {
    m_models.push_back(d.model);
    m_normals.push_back(glm::mat4(d.normalMatrix));  // mat3 in the upper-left 3x3
}

const std::vector<InstanceRun>& GLInstanceBatcher::buildGrouped(
    const std::vector<const DrawableData*>& list, const GLView& view) {
    m_runs.clear();
    m_models.clear();
    m_normals.clear();
    if (list.empty()) return m_runs;

    // Sort indices by (material id, mesh id) so identical draws sit contiguously
    // and merge into one instanced call each. Sorting indices keeps the input
    // list untouched and avoids moving the ~100-byte DrawableData pointers' targets.
    m_order.resize(list.size());
    for (uint32_t i = 0; i < list.size(); ++i) m_order[i] = i;
    std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
        const DrawableData* da = list[a];
        const DrawableData* db = list[b];
        if (da->material.id() != db->material.id()) return da->material.id() < db->material.id();
        return da->mesh.id() < db->mesh.id();
    });

    m_models.reserve(list.size());
    m_normals.reserve(list.size());

    uint32_t i = 0;
    while (i < m_order.size()) {
        const DrawableData* head   = list[m_order[i]];
        const GLMesh*       mesh   = view.getMesh(head->mesh);
        const uint32_t      matId  = head->material.id();
        const uint32_t      meshId = head->mesh.id();
        const uint32_t      first  = static_cast<uint32_t>(m_models.size());

        uint32_t j = i;
        while (j < m_order.size()) {
            const DrawableData* d = list[m_order[j]];
            if (d->material.id() != matId || d->mesh.id() != meshId) break;
            append(*d);
            ++j;
        }

        const uint32_t count = static_cast<uint32_t>(m_models.size()) - first;
        if (mesh && count > 0) m_runs.push_back({ mesh, head->material, first, count });
        i = j;
    }

    upload();
    return m_runs;
}

const std::vector<InstanceRun>& GLInstanceBatcher::buildSequential(
    const std::vector<const DrawableData*>& list, const GLView& view) {
    m_runs.clear();
    m_models.clear();
    m_normals.clear();
    if (list.empty()) return m_runs;

    m_models.reserve(list.size());
    m_normals.reserve(list.size());

    // One instance per drawable, input order preserved (depth order for
    // transparents). Each is its own single-instance run.
    for (const DrawableData* d : list) {
        const GLMesh* mesh = view.getMesh(d->mesh);
        if (!mesh) continue;
        const uint32_t first = static_cast<uint32_t>(m_models.size());
        append(*d);
        m_runs.push_back({ mesh, d->material, first, 1 });
    }

    upload();
    return m_runs;
}

void GLInstanceBatcher::upload() {
    m_modelBuffer.update(m_models.data(),  static_cast<uint32_t>(m_models.size()));
    m_normalBuffer.update(m_normals.data(), static_cast<uint32_t>(m_normals.size()));
}

void GLInstanceBatcher::drawRun(const InstanceRun& run) {
    run.mesh->attachInstances(m_modelBuffer,  MODEL_ATTRIB);
    run.mesh->attachInstances(m_normalBuffer, NORMAL_ATTRIB);
    run.mesh->drawInstanced(run.count, run.first);
}

} // namespace Engine
