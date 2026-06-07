#include "debug/gpu_timing.h"

#include <algorithm>
#include <utility>

namespace Engine {

GpuTimingPool& GpuTimingPool::get() {
    static GpuTimingPool instance;
    return instance;
}

void GpuTimingPool::resize(std::size_t passCount) {    if (m_passes.size() == passCount) return;
    m_passes.resize(passCount);
}

void GpuTimingPool::setPassName(std::size_t passIndex, std::string name) {    if (passIndex >= m_passes.size()) return;
    if (m_passes[passIndex].name != name) {
        m_passes[passIndex].name = std::move(name);
    }
}

void GpuTimingPool::recordSample(std::size_t passIndex, double ms) {    if (passIndex >= m_passes.size()) return;
    PassStats& p = m_passes[passIndex];
    p.ring[p.cursor] = static_cast<float>(ms);
    p.cursor = (p.cursor + 1) % RING_SIZE;
    p.sampleCount = std::min(p.sampleCount + 1, RING_SIZE);
    p.last = ms;
    recomputeDerived(p);
}

std::vector<GpuTimingPool::PassStats> GpuTimingPool::snapshot() const {    return m_passes;
}

std::size_t GpuTimingPool::passCount() const {    return m_passes.size();
}

void GpuTimingPool::recomputeDerived(PassStats& p) const {
    if (p.sampleCount == 0) {
        p.avg = p.p99 = p.maxV = 0.0;
        return;
    }
    // Walk the valid-prefix only - the ring is zeroed at construction
    // but we don't want zeros pulling the average down before RING_SIZE
    // samples have landed.
    std::size_t n = p.sampleCount;
    double sum = 0.0;
    double maxV = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = static_cast<double>(p.ring[i]);
        sum += v;
        if (v > maxV) maxV = v;
    }
    p.avg  = sum / static_cast<double>(n);
    p.maxV = maxV;

    // Cheap p99: sort a stack-bounded scratch copy. n <= RING_SIZE = 120
    // so allocating on the stack is fine.
    float scratch[RING_SIZE];
    for (std::size_t i = 0; i < n; ++i) scratch[i] = p.ring[i];
    std::sort(scratch, scratch + n);
    const std::size_t idx = (n * 99) / 100;
    p.p99 = static_cast<double>(scratch[std::min(idx, n - 1)]);
}

} // namespace Engine
