#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_instance_batcher.h"

#include <algorithm>

#include "convention/gl_bindings.h"
#include "gl_view.h"
#include "data/gl_mesh.h"
#include "system/render/data/drawable_data.h"

namespace Engine {

namespace {

namespace SSBO = GLBindings::SSBOBindingPoints;

/**
 * @brief Grow-or-update a stream buffer to hold @p bytes of @p data.
 *
 * The batch buffers size to the frame's instance count, which moves;
 * reallocating only when it grows keeps a steady scene at one allocation.
 *
 * @tparam BufferT   Core buffer type to allocate (vertex or shader storage).
 * @param buffer     The buffer, allocated on first use and on every grow.
 * @param capacity   Bytes currently allocated; updated when it grows.
 * @param data       Source bytes to upload.
 * @param bytes      How many of them; 0 leaves the buffer untouched.
 */
template <typename BufferT>
void growAndUpload(std::unique_ptr<BufferT>& buffer, uint32_t& capacity,
                   const void* data, uint32_t bytes) {
    if (bytes == 0) return;
    if (!buffer || bytes > capacity) {
        capacity = bytes + bytes / 2;   // headroom, so a growing frame does not realloc every time
        buffer = std::make_unique<BufferT>(nullptr, capacity, GL_STREAM_DRAW);
    }
    buffer->update(data, bytes, 0);
}

} // namespace

void GLInstanceBatcher::append(const DrawableData& d, uint32_t runIndex) {
    m_models.push_back(d.model);
    m_normals.push_back(glm::mat4(d.normalMatrix));  // mat3 in the upper-left 3x3
    m_bounds.emplace_back(d.worldMin, 0.0f);
    m_bounds.emplace_back(d.worldMax, 0.0f);
    m_runOf.push_back(runIndex);
}

const std::vector<InstanceRun>& GLInstanceBatcher::buildGrouped(
    const std::vector<const DrawableData*>& list, const GLView& view) {
    m_runs.clear();
    m_models.clear();
    m_normals.clear();
    m_bounds.clear();
    m_runOf.clear();
    if (list.empty()) return m_runs;

    // Sort indices by (material id, mesh id) so identical draws sit contiguously
    // and merge into one instanced call each. Sorting indices keeps the input
    // list untouched.
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
            // A group with no GL mesh pushes no run below, so its instances must
            // not enter the flattened arrays either: append stamps them with the
            // index the NEXT run takes, and the GPU cull compacts by that index.
            if (mesh) append(*d, static_cast<uint32_t>(m_runs.size()));
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
    m_bounds.clear();
    m_runOf.clear();
    if (list.empty()) return m_runs;

    m_models.reserve(list.size());
    m_normals.reserve(list.size());

    for (const DrawableData* d : list) {
        const GLMesh* mesh = view.getMesh(d->mesh);
        if (!mesh) continue;
        const uint32_t first = static_cast<uint32_t>(m_models.size());
        append(*d, static_cast<uint32_t>(m_runs.size()));
        m_runs.push_back({ mesh, d->material, first, 1 });
    }

    upload();
    return m_runs;
}

void GLInstanceBatcher::upload() {
    const auto count = static_cast<uint32_t>(m_models.size());
    m_modelBuffer.update(m_models.data(),  count);
    m_normalBuffer.update(m_normals.data(), count);

    // Identity to start with: an un-culled batch draws every instance in batch
    // order, and a cull - when one runs - overwrites this with the survivors.
    // Filling it here means the draw path has exactly one shape whether or not
    // anything culled.
    m_visible.resize(count);
    for (uint32_t i = 0; i < count; ++i) m_visible[i] = i;
    growAndUpload(m_visibleBuffer, m_visibleCapacity, m_visible.data(),
                  static_cast<uint32_t>(count * sizeof(uint32_t)));

    growAndUpload(m_boundsBuffer, m_boundsCapacity, m_bounds.data(),
                  static_cast<uint32_t>(m_bounds.size() * sizeof(glm::vec4)));
    growAndUpload(m_runOfBuffer, m_runOfCapacity, m_runOf.data(),
                  static_cast<uint32_t>(m_runOf.size() * sizeof(uint32_t)));

    resetCommands();

    // A fresh batch has no cull behind it yet; the next bindCullBuffers sets it.
    m_culled = false;
}

void GLInstanceBatcher::resetCommands() {
    m_commands.resize(m_runs.size());
    for (size_t i = 0; i < m_runs.size(); ++i) {
        const InstanceRun& run = m_runs[i];
        m_commands[i] = DrawCommand{
            run.mesh ? run.mesh->indexCount() : 0u,
            0u,              // the cull counts up from empty
            0u, 0u,
            run.first,       // the run's slice, which compaction stays inside
        };
    }
    growAndUpload(m_commandBuffer, m_commandCapacity, m_commands.data(),
                  static_cast<uint32_t>(m_commands.size() * sizeof(DrawCommand)));
}

bool GLInstanceBatcher::bindCullBuffers() {
    if (m_models.empty() || !m_visibleBuffer || !m_boundsBuffer || !m_runOfBuffer
        || !m_commandBuffer) return false;

    m_boundsBuffer->bindBase(SSBO::CullBounds);
    m_runOfBuffer->bindBase(SSBO::CullRunIndex);
    m_commandBuffer->bindBase(SSBO::CullCommands);
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CullVisible, m_visibleBuffer->getID()));

    // From here the frame's draws read the index list the cull is about to write.
    m_culled = true;
    return true;
}

void GLInstanceBatcher::bindInstanceData() const {
    // Just the transforms. The index buffer is bound as a vertex attribute, not
    // as storage - the fetch resolves the indirection, so the shader never
    // reads it.
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::InstanceModels,  m_modelBuffer.id()));
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::InstanceNormals, m_normalBuffer.id()));
}

void GLInstanceBatcher::drawRun(const InstanceRun& run, uint32_t runIndex) {
    run.mesh->attachInstanceIndex(*m_visibleBuffer);

    if (m_culled && m_commandBuffer && runIndex < m_commands.size()) {
        m_commandBuffer->bind(GL_DRAW_INDIRECT_BUFFER);
        run.mesh->drawIndirect(runIndex * static_cast<uint32_t>(sizeof(DrawCommand)));
        return;
    }

    run.mesh->drawInstanced(run.count, run.first);
}

} // namespace Engine
