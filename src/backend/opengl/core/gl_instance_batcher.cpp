#include "gl_instance_batcher.h"

#include "gl_instance_buffer.h"
#include "render_view.h"

namespace Engine {

void GLInstanceBatcher::build(const std::vector<DrawableData>& drawables) {
    m_batches.clear();

    if (drawables.empty()) {
        return;
    }

    // Single pass through sorted drawables to build batches
    // Drawables are pre-sorted by (material, mesh)
    size_t batchStart = 0;
    uint32_t currentMesh = drawables[0].mesh.value;
    uint32_t currentMaterial = drawables[0].material.value;

    for (size_t i = 1; i <= drawables.size(); ++i) {
        bool endOfBatch = (i == drawables.size()) ||
                          (drawables[i].mesh.value != currentMesh) ||
                          (drawables[i].material.value != currentMaterial);

        if (endOfBatch) {
            uint32_t instanceCount = static_cast<uint32_t>(i - batchStart);

            // Collect matrices for this batch
            m_matrixScratch.clear();
            m_matrixScratch.reserve(instanceCount);
            for (size_t j = batchStart; j < i; ++j) {
                m_matrixScratch.push_back(drawables[j].model);
            }

            // Create or reuse instance buffer for this batch
            size_t batchIndex = m_batches.size();
            if (batchIndex >= m_instanceBuffers.size()) {
                m_instanceBuffers.push_back(std::make_unique<GLInstanceBuffer>());
            }
            m_instanceBuffers[batchIndex]->update(m_matrixScratch, instanceCount);

            // Create batch descriptor
            InstanceBatch batch;
            batch.mesh.value = currentMesh;
            batch.material.value = currentMaterial;
            batch.instanceCount = instanceCount;
            m_batches.push_back(batch);

            // Start new batch if not at end
            if (i < drawables.size()) {
                batchStart = i;
                currentMesh = drawables[i].mesh.value;
                currentMaterial = drawables[i].material.value;
            }
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
