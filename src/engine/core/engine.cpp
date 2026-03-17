#include "core/engine.h"

#include <algorithm>
#include <cstdio>
#include <chrono>

#include "logger.h"

namespace Engine {

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

void Engine::run() {
    while (m_window.beginFrame()) {
        float deltaTime = m_statistics.getFrameInfo().frameRateInfo.frameTime / 1000.0f;

        if (!m_window.updateInput()) break;

        FrameContext ctx{
            m_scene, m_resources, m_window, m_statistics,
            deltaTime,
            static_cast<uint32_t>(m_window.getWidth()),
            static_cast<uint32_t>(m_window.getHeight()),
            nullptr
        };

        if (!m_initialized) {
            initSystems(ctx);
        }

        for (auto& system : m_systems) {
            if (system->isEnabled()) {
                system->update(ctx);
            }
        }

        if (!m_window.swapBuffers()) break;

        m_statistics.update();
        printStats(ctx);
    }

    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    // Validate system access declarations for write-write conflicts
    for (size_t i = 0; i < m_systems.size(); ++i) {
        auto accessA = m_systems[i]->declareAccess();
        for (size_t j = i + 1; j < m_systems.size(); ++j) {
            auto accessB = m_systems[j]->declareAccess();
            for (TypeId wa : accessA.writes) {
                if (std::find(accessB.writes.begin(), accessB.writes.end(), wa) != accessB.writes.end()) {
                    LOG_WARNING("Systems %zu and %zu both write to component type %u", i, j, wa);
                }
            }
        }
    }

    for (auto& system : m_systems) {
        system->init(ctx);
    }
    m_initialized = true;
}

void Engine::shutdownSystems() {
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        (*it)->shutdown();
    }
}

void Engine::printStats(const FrameContext& ctx) {
    static std::chrono::steady_clock::time_point lastStatsPrint;
    auto now = std::chrono::steady_clock::now();
    if (now - lastStatsPrint < std::chrono::milliseconds(500)) return;

    const auto& info = m_statistics.getFrameInfo();
    LOG_VERBOSE("[%lu] %.2fms (%.0f FPS) | Draws: %u | Passes: %u | Visible: %zu/%zu\n",
        info.frameIndex,
        info.frameRateInfo.frameTime,
        info.frameRateInfo.frameRate,
        info.renderSystemInfo.drawCalls,
        info.renderSystemInfo.renderPasses,
        ctx.visibility ? ctx.visibility->entries.size() : 0,
        m_scene.entityCount()
    );
    lastStatsPrint = now;
}

} // namespace Engine
