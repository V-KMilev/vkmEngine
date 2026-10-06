#include "system/animation/pose_buffer.h"

#include <algorithm>
#include <limits>

namespace Vkm::Engine {

void PoseBuffer::clear() {
    m_global.clear();
    m_palette.clear();
    m_slices.clear();
    // Not cleared: addressed by entity slot, it keeps its length and forgets its contents.
    std::fill(m_sliceOfEntity.begin(), m_sliceOfEntity.end(), Mapping{});
}

uint32_t PoseBuffer::addSlice(uint32_t boneCount) {
    const auto first = static_cast<uint32_t>(m_global.size());
    m_global.resize(first + boneCount);
    m_palette.resize(first + boneCount);

    const auto index = static_cast<uint32_t>(m_slices.size());
    PoseSlice slice;
    slice.first = first;
    slice.count = boneCount;
    m_slices.push_back(slice);
    return index;
}

void PoseBuffer::mapEntity(EntityId entity, uint32_t slice) {
    const uint32_t slot = entity.slot();
    if (slot >= m_sliceOfEntity.size()) m_sliceOfEntity.resize(slot + 1);
    m_sliceOfEntity[slot] = Mapping{entity, slice};
}

PoseWrite PoseBuffer::writeTo(uint32_t slice) {
    PoseSlice& s = m_slices[slice];
    return PoseWrite{&s, m_global.data() + s.first, m_palette.data() + s.first};
}

const PoseSlice* PoseBuffer::sliceOf(EntityId entity) const {
    if (!entity || entity.slot() >= m_sliceOfEntity.size()) return nullptr;
    const Mapping& mapping = m_sliceOfEntity[entity.slot()];
    return mapping.entity == entity ? &m_slices[mapping.slice] : nullptr;
}

void finishSlice(const PoseWrite& out) {
    PoseSlice& slice = *out.slice;
    if (slice.count == 0) {
        slice.originMin    = glm::vec3(0.0f);
        slice.originMax    = glm::vec3(0.0f);
        slice.maxBoneScale = 1.0f;
        return;
    }

    glm::vec3 originMin(std::numeric_limits<float>::max());
    glm::vec3 originMax(std::numeric_limits<float>::lowest());
    float maxScale = 1.0f;

    for (uint32_t i = 0; i < slice.count; ++i) {
        const glm::mat4& bone = out.global[i];
        originMin = glm::min(originMin, glm::vec3(bone[3]));
        originMax = glm::max(originMax, glm::vec3(bone[3]));

        // Accumulated, not local: the total stretches the skin. Floored at 1, because an under-sized
        // bound deletes visible geometry.
        maxScale = std::max({
            maxScale,
            glm::length(glm::vec3(bone[0])),
            glm::length(glm::vec3(bone[1])),
            glm::length(glm::vec3(bone[2]))
        });
    }

    slice.originMin    = originMin;
    slice.originMax    = originMax;
    slice.maxBoneScale = maxScale;
}

} // namespace Vkm::Engine
