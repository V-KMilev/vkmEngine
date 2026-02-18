#include "core/engine.h"

#include <cstdio>
#include <chrono>

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
            m_scene, m_resources, deltaTime,
            static_cast<uint32_t>(m_window.getWidth()),
            static_cast<uint32_t>(m_window.getHeight()),
            {}
        };

        for (auto& system : m_systems) {
            system->update(ctx);
        }

        if (!m_window.swapBuffers()) break;

        m_statistics.update();
        printStats(ctx);
    }
}

void Engine::printStats(const FrameContext& ctx) {
    static std::chrono::steady_clock::time_point lastStatsPrint;
    auto now = std::chrono::steady_clock::now();
    if (now - lastStatsPrint < std::chrono::milliseconds(500)) return;

    const auto& info = m_statistics.getFrameInfo();
    std::printf("[%lu] FPS: %.2f (%.4fms) | Draws: %u | Visible: %zu/%u\n",
        info.frameIndex,
        info.frameRateInfo.frameRate,
        info.frameRateInfo.frameTime,
        info.renderSystemInfo.drawCalls,
        ctx.visibility.entities.size(),
        info.entitySystemInfo.entityUpdates
    );
    std::fflush(stdout);
    lastStatsPrint = now;
}

} // namespace Engine
