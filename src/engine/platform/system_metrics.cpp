#include "platform/system_metrics.h"

#include <GL/glew.h>
#include <chrono>
#include <cstdio>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <psapi.h>
#else
    #include <fstream>
    #include <unistd.h>
    #include <dlfcn.h>
#endif

// Minimal NVML types for dynamic loading
namespace {
    enum nvmlReturn_t { NVML_SUCCESS = 0 };
    struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
    using nvmlDevice_t = void*;

    using nvmlInit_fn                      = nvmlReturn_t (*)();
    using nvmlShutdown_fn                  = nvmlReturn_t (*)();
    using nvmlDeviceGetHandleByIndex_fn    = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using nvmlDeviceGetUtilizationRates_fn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
}

// GL extension fallback defines
#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX   0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif

namespace Engine {

SystemMetrics::SystemMetrics() {
    initNvml();
}

SystemMetrics::~SystemMetrics() {
    shutdownNvml();
}

void SystemMetrics::initNvml() {
#ifdef _WIN32
    m_nvmlLib = LoadLibraryA("nvml.dll");
#else
    m_nvmlLib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!m_nvmlLib) m_nvmlLib = dlopen("libnvidia-ml.so", RTLD_LAZY);
#endif
    if (!m_nvmlLib) return;

#ifdef _WIN32
    auto getSym = [&](const char* name) { return (void*)GetProcAddress((HMODULE)m_nvmlLib, name); };
#else
    auto getSym = [&](const char* name) { return dlsym(m_nvmlLib, name); };
#endif

    auto init   = (nvmlInit_fn)getSym("nvmlInit_v2");
    auto getDev = (nvmlDeviceGetHandleByIndex_fn)getSym("nvmlDeviceGetHandleByIndex_v2");
    m_nvmlGetUtilRates = getSym("nvmlDeviceGetUtilizationRates");

    if (!init || !getDev || !m_nvmlGetUtilRates) {
        shutdownNvml();
        return;
    }

    if (init() != NVML_SUCCESS) {
        shutdownNvml();
        return;
    }

    nvmlDevice_t dev = nullptr;
    if (getDev(0, &dev) != NVML_SUCCESS) {
        auto shutdown = (nvmlShutdown_fn)getSym("nvmlShutdown");
        if (shutdown) shutdown();
        shutdownNvml();
        return;
    }
    m_nvmlDevice = dev;
}

void SystemMetrics::shutdownNvml() {
    if (m_nvmlLib) {
#ifdef _WIN32
        auto getSym = [&](const char* name) { return (void*)GetProcAddress((HMODULE)m_nvmlLib, name); };
#else
        auto getSym = [&](const char* name) { return dlsym(m_nvmlLib, name); };
#endif
        if (m_nvmlDevice) {
            auto shutdown = (nvmlShutdown_fn)getSym("nvmlShutdown");
            if (shutdown) shutdown();
        }
#ifdef _WIN32
        FreeLibrary((HMODULE)m_nvmlLib);
#else
        dlclose(m_nvmlLib);
#endif
    }
    m_nvmlLib = nullptr;
    m_nvmlDevice = nullptr;
    m_nvmlGetUtilRates = nullptr;
}

void SystemMetrics::update(float deltaTime) {
    m_timer += deltaTime;
    if (m_timer < 0.5f) return;
    m_timer = 0.0f;

    // ── CPU usage (process) ──
#ifdef _WIN32
    {
        FILETIME creation, exit, kernel, user;
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            uint64_t k = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
            uint64_t u = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
            uint64_t cpuTime = k + u;

            using Clock = std::chrono::high_resolution_clock;
            static auto lastWall = Clock::now();
            auto now = Clock::now();
            double wallSec = std::chrono::duration<double>(now - lastWall).count();
            lastWall = now;

            if (m_prevCpuTime > 0 && wallSec > 0.0) {
                double cpuSec = double(cpuTime - m_prevCpuTime) * 1e-7;
                SYSTEM_INFO si; GetSystemInfo(&si);
                m_cpuPercent = float(cpuSec / wallSec / si.dwNumberOfProcessors * 100.0);
            }
            m_prevCpuTime = cpuTime;
        }
    }
#else
    {
        std::ifstream stat("/proc/self/stat");
        if (stat.is_open()) {
            std::string skip;
            unsigned long utime = 0, stime = 0;
            stat >> skip; // pid
            stat >> skip; // comm
            stat >> skip; // state
            for (int i = 0; i < 10; ++i) stat >> skip; // fields 4-13
            stat >> utime >> stime;

            uint64_t cpuTicks = utime + stime;

            using Clock = std::chrono::high_resolution_clock;
            static auto lastWall = Clock::now();
            auto now = Clock::now();
            double wallSec = std::chrono::duration<double>(now - lastWall).count();
            lastWall = now;

            if (m_prevCpuTime > 0 && wallSec > 0.0) {
                long hz = sysconf(_SC_CLK_TCK);
                long cores = sysconf(_SC_NPROCESSORS_ONLN);
                double cpuSec = double(cpuTicks - m_prevCpuTime) / double(hz);
                m_cpuPercent = float(cpuSec / wallSec / double(cores) * 100.0);
            }
            m_prevCpuTime = cpuTicks;
        }
    }
#endif

    // ── RAM (process RSS + total system) ──
#ifdef _WIN32
    {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            m_ramUsedMB = float(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus))
            m_ramTotalMB = float(double(memStatus.ullTotalPhys) / (1024.0 * 1024.0));
    }
#else
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0)
            m_ramTotalMB = float(double(pages) * double(pageSize) / (1024.0 * 1024.0));

        std::ifstream status("/proc/self/status");
        if (status.is_open()) {
            std::string line;
            while (std::getline(status, line)) {
                if (line.compare(0, 6, "VmRSS:") == 0) {
                    unsigned long rssKB = 0;
                    std::sscanf(line.c_str(), "VmRSS: %lu", &rssKB);
                    m_ramUsedMB = float(rssKB) / 1024.0f;
                    break;
                }
            }
        }
    }
#endif

    // ── GPU VRAM (NVIDIA or AMD GL extensions) ──
    {
        GLint totalKB = 0, availKB = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKB);
        if (glGetError() == GL_NO_ERROR && totalKB > 0) {
            glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availKB);
            glGetError();
            m_vramTotalMB = float(totalKB) / 1024.0f;
            m_vramUsedMB  = float(totalKB - availKB) / 1024.0f;
        } else {
            GLint memInfo[4] = {};
            glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, memInfo);
            if (glGetError() == GL_NO_ERROR && memInfo[0] > 0) {
                m_vramUsedMB  = 0.0f;
                m_vramTotalMB = 0.0f;
            }
        }
    }

    // ── GPU Utilization (NVML) ──
    if (m_nvmlDevice && m_nvmlGetUtilRates) {
        nvmlUtilization_t util{};
        auto fn = (nvmlDeviceGetUtilizationRates_fn)m_nvmlGetUtilRates;
        if (fn((nvmlDevice_t)m_nvmlDevice, &util) == NVML_SUCCESS)
            m_gpuPercent = float(util.gpu);
    }
}

} // namespace Engine
