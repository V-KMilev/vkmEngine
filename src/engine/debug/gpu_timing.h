#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief Backend-agnostic per-pass GPU timing collection.
 *
 * Engine owns the ring buffers; backends measure and push completed
 * timings via @ref recordSample(). The render graph wraps each pass'
 * execute() with backend timer primitives and the backend drains
 * completed queries inside its endFrame() hook.
 */
class GpuTimingPool {
    public:
        GpuTimingPool(const GpuTimingPool& other) = delete;
        GpuTimingPool& operator=(const GpuTimingPool& other) = delete;

        GpuTimingPool(GpuTimingPool && other) = delete;
        GpuTimingPool& operator=(GpuTimingPool && other) = delete;

    public:
        static constexpr std::size_t RING_SIZE = 120;

        struct PassStats {
            std::string name;
            double      last = 0.0;            ///< Most recently recorded sample, ms.
            double      avg  = 0.0;            ///< Average over the ring, ms.
            double      p99  = 0.0;            ///< 99th percentile, ms.
            double      maxV = 0.0;            ///< Ring-wide maximum, ms.
            std::size_t sampleCount = 0;       ///< Total samples seen this session (clamped to RING_SIZE for averaging).
            std::array<float, RING_SIZE> ring{};  ///< Most recent samples (float for ImGui plot consumption).
            std::size_t cursor = 0;            ///< Index of the next write slot.
        };

        static GpuTimingPool& get();

        /// Re-size the pool to track @p passCount passes. Idempotent; preserves
        /// existing data when @p passCount didn't change.
        void resize(std::size_t passCount);

        /// Update the displayed name for a pass. Called by the graph each
        /// frame; cheap, idempotent.
        void setPassName(std::size_t passIndex, std::string name);

        /// Push a measured time-in-ms sample for one pass. Backends call
        /// this from their endFrame() drain.
        void recordSample(std::size_t passIndex, double ms);

        /// Snapshot for the editor UI. Returns an empty vector if no passes.
        std::vector<PassStats> snapshot() const;

        std::size_t passCount() const;

    private:
        GpuTimingPool() = default;

        void recomputeDerived(PassStats& p) const;

        std::vector<PassStats> m_passes;
};

} // namespace Engine
