#pragma once

#include <cstdint>

namespace Engine {

class SystemMetrics {
    public:
        SystemMetrics();
        ~SystemMetrics();

        SystemMetrics(const SystemMetrics&) = delete;
        SystemMetrics& operator=(const SystemMetrics&) = delete;
        SystemMetrics(SystemMetrics&&) = delete;
        SystemMetrics& operator=(SystemMetrics&&) = delete;

        void update(float deltaTime);

        float cpuPercent() const { return m_cpuPercent; }
        float ramUsedMB()  const { return m_ramUsedMB; }
        float ramTotalMB() const { return m_ramTotalMB; }
        float gpuPercent() const { return m_gpuPercent; }
        float vramUsedMB() const { return m_vramUsedMB; }
        float vramTotalMB() const { return m_vramTotalMB; }
        bool  hasGpuUtil()  const { return m_nvmlDevice != nullptr; }
        bool  hasVram()     const { return m_vramTotalMB > 0.0f; }

    private:
        void initNvml();
        void shutdownNvml();

        float m_timer       = 0.0f;
        float m_cpuPercent  = 0.0f;
        float m_ramUsedMB   = 0.0f;
        float m_ramTotalMB  = 0.0f;
        float m_gpuPercent  = 0.0f;
        float m_vramUsedMB  = 0.0f;
        float m_vramTotalMB = 0.0f;

        uint64_t m_prevCpuTime = 0;

        void* m_nvmlLib          = nullptr;
        void* m_nvmlDevice       = nullptr;
        void* m_nvmlGetUtilRates = nullptr;
};

} // namespace Engine
