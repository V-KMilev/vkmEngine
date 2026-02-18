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
    MeshHandle currentMesh = drawables[0].mesh;
    MaterialHandle currentMaterial = drawables[0].material;

    for (size_t i = 1; i <= drawables.size(); ++i) {
        bool endOfBatch = (i == drawables.size()) ||
                          (drawables[i].mesh != currentMesh) ||
                          (drawables[i].material != currentMaterial);

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
            batch.mesh = currentMesh;
            batch.material = currentMaterial;
            batch.instanceCount = instanceCount;
            m_batches.push_back(batch);

            // Start new batch if not at end
            if (i < drawables.size()) {
                batchStart = i;
                currentMesh = drawables[i].mesh;
                currentMaterial = drawables[i].material;
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
