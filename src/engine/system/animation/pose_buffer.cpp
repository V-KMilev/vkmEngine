#include "system/animation/pose_buffer.h"

#include <algorithm>

namespace Vkm::Engine {

void PoseBuffer::clear() {
    m_global.clear();
    m_palette.clear();
    m_slices.clear();
    // Not cleared: the map is addressed by entity slot, so it has to stay the
    // length it grew to and simply forget what it held.
    std::fill(m_sliceOfEntity.begin(), m_sliceOfEntity.end(), NO_POSE);
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

void PoseBuffer::mapEntity(uint32_t entityIndex, uint32_t slice) {
    if (entityIndex >= m_sliceOfEntity.size()) m_sliceOfEntity.resize(entityIndex + 1, NO_POSE);
    m_sliceOfEntity[entityIndex] = slice;
}

PoseWrite PoseBuffer::writeTo(uint32_t slice) {
    PoseSlice& s = m_slices[slice];
    return PoseWrite{&s, m_global.data() + s.first, m_palette.data() + s.first};
}

const PoseSlice* PoseBuffer::sliceOf(uint32_t entityIndex) const {
    if (entityIndex >= m_sliceOfEntity.size()) return nullptr;
    const uint32_t slice = m_sliceOfEntity[entityIndex];
    return slice == NO_POSE ? nullptr : &m_slices[slice];
}

} // namespace Vkm::Engine
