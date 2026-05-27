#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_instance_batcher.h"

#include "gl_vertex_array.h"  // Core::VertexArray
#include "system/render/render_view.h"

namespace Engine {

std::unordered_map<uint32_t, const GLInstanceBatcher*> GLInstanceBatcher::s_vaoOwner;

void GLInstanceBatcher::attachToVAO(Core::VertexArray& vao, uint32_t startIndex) {
    const uint32_t vaoId = vao.getID();
    const GLInstanceBatcher*& owner = s_vaoOwner[vaoId];
    if (owner == this) return;   // VAO's instanced attribs already point here
    owner = this;
    m_buffer.attachToVAO(vao, startIndex);
}

void GLInstanceBatcher::build(const std::vector<DrawableData>& drawables) {
    m_batches.clear();

    if (drawables.empty()) {
        return;
    }

    // Concatenate all model matrices in draw order. Drawables are pre-sorted by
    // (materialType, material, mesh) so each batch is a contiguous run.
    m_matrixScratch.resize(drawables.size());
    for (size_t i = 0; i < drawables.size(); ++i) {
        m_matrixScratch[i] = drawables[i].model;
    }

    size_t batchStart = 0;
    for (size_t i = 1; i <= drawables.size(); ++i) {
        bool endOfBatch = (i == drawables.size());
        if (!endOfBatch) {
            endOfBatch = (drawables[i].materialType != drawables[batchStart].materialType) ||
                         (drawables[i].mesh != drawables[batchStart].mesh) ||
                         (drawables[i].material != drawables[batchStart].material);
        }

        if (endOfBatch) {
            InstanceBatch batch;
            batch.mesh          = drawables[batchStart].mesh;
            batch.material      = drawables[batchStart].material;
            batch.materialType  = drawables[batchStart].materialType;
            batch.instanceCount = static_cast<uint32_t>(i - batchStart);
            batch.firstInstance = static_cast<uint32_t>(batchStart);

            // castShadows=true entries are sorted to the front of each batch
            // (see RenderView::sortDrawables), so the shadow-castable instances
            // form a contiguous prefix [firstInstance, firstInstance + shadowInstanceCount).
            uint32_t shadowCount = 0;
            for (size_t k = batchStart; k < i; ++k) {
                if (!drawables[k].castShadows) break;
                ++shadowCount;
            }
            batch.shadowInstanceCount = shadowCount;

            m_batches.push_back(batch);

            batchStart = i;
        }
    }

    // Single GPU upload for the entire frame
    m_buffer.update(m_matrixScratch.data(), static_cast<uint32_t>(m_matrixScratch.size()));
}

void GLInstanceBatcher::clear() {
    m_batches.clear();
}

} // namespace Engine
