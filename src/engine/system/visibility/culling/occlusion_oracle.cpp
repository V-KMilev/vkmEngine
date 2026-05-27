#include "system/visibility/culling/occlusion_oracle.h"

namespace Engine {

OcclusionOracle& OcclusionOracle::get() {
    static OcclusionOracle instance;
    return instance;
}

void OcclusionOracle::publish(std::vector<float> pyramid,
                              std::uint32_t      width,
                              std::uint32_t      height,
                              const glm::mat4&   viewProj) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current.pyramid  = std::move(pyramid);
    m_current.width    = width;
    m_current.height   = height;
    m_current.viewProj = viewProj;
    m_current.ready    = (width > 0 && height > 0 && !m_current.pyramid.empty());
}

OcclusionOracle::Frame OcclusionOracle::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current;
}

void OcclusionOracle::invalidate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current.ready = false;
    m_current.pyramid.clear();
}

} // namespace Engine
