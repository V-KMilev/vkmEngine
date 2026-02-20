#include "gl_instance_batcher.h"

#include "gl_instance_buffer.h"
#include "render/render_view.h"

namespace Engine {

void GLInstanceBatcher::build(const std::vector<DrawableData>& drawables) {
    m_batches.clear();

    if (drawables.empty()) {
        return;
    }

    // Single pass through sorted drawables to build batches
    // Drawables are pre-sorted by (material, mesh)
    size_t batchStart = 0;

    for (size_t i = 1; i <= drawables.size(); ++i) {
        bool endOfBatch = (i == drawables.size());
        if (!endOfBatch) {
            endOfBatch = (drawables[i].mesh != drawables[batchStart].mesh) ||
                         (drawables[i].material != drawables[batchStart].material);
        }

        if (endOfBatch) {
            uint32_t instanceCount = static_cast<uint32_t>(i - batchStart);

            // Gather matrices for this batch into scratch buffer
            m_matrixScratch.resize(instanceCount);
            for (size_t j = 0; j < instanceCount; ++j) {
                m_matrixScratch[j] = drawables[batchStart + j].model;
            }

            // Create or reuse instance buffer for this batch
            size_t batchIndex = m_batches.size();
            if (batchIndex >= m_instanceBuffers.size()) {
                m_instanceBuffers.push_back(std::make_unique<GLInstanceBuffer>());
            }
            m_instanceBuffers[batchIndex]->update(m_matrixScratch.data(), instanceCount);

            // Create batch descriptor
            InstanceBatch batch;
            batch.mesh = drawables[batchStart].mesh;
            batch.material = drawables[batchStart].material;
            batch.instanceCount = instanceCount;
            m_batches.push_back(batch);

            batchStart = i;
        }
    }
}

GLInstanceBuffer* GLInstanceBatcher::getInstanceBuffer(size_t batchIndex) {
    if (batchIndex >= m_instanceBuffers.size()) {
        return nullptr;
    }
    return m_instanceBuffers[batchIndex].get();
}

void GLInstanceBatcher::clear() {
    m_batches.clear();
    // Keep instance buffers allocated for reuse
}

} // namespace Engine
