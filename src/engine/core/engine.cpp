#include "core/engine.h"

#include <algorithm>
#include <cstdio>
#include <chrono>

#include "logger.h"

namespace Engine {

namespace {
    // Fixed simulation step (60 Hz). Determines fixedUpdate cadence.
    constexpr float FIXED_DT = 1.0f / 60.0f;
    // Cap on the simulation-time accumulator. Without this, a hitch (debugger
    // pause, swap-to-background, etc.) would queue up enough fixedUpdate ticks
    // to take longer than the frame budget, making the next frame even slower
    // ("spiral of death"). 0.25s ≈ 15 ticks max per render frame.
    constexpr float MAX_ACCUMULATOR = 0.25f;
}

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

void Engine::run() {
    float accumulator = 0.0f;

    while (m_window.beginFrame()) {
        float deltaTime = m_statistics.getFrameInfo().frameRateInfo.frameTime / 1000.0f;

        if (!m_window.updateInput()) break;

        FrameContext ctx{
            m_scene, m_resources, m_window, m_statistics,
            deltaTime,
            FIXED_DT,
            static_cast<uint32_t>(m_window.getWidth()),
            static_cast<uint32_t>(m_window.getHeight()),
            nullptr
        };

        if (!m_initialized) {
            initSystems(ctx);
        }

        accumulator = std::min(accumulator + deltaTime, MAX_ACCUMULATOR);
        while (accumulator >= FIXED_DT) {
            for (auto& stage : m_systemsByStage) {
                for (auto& system : stage) {
                    if (system->isEnabled()) {
                        system->fixedUpdate(ctx);
                    }
                }
            }
            accumulator -= FIXED_DT;
        }

        for (auto& stage : m_systemsByStage) {
            for (auto& system : stage) {
                if (system->isEnabled()) {
                    system->update(ctx);
                }
            }
        }

        if (!m_window.swapBuffers()) break;

        m_statistics.update();
        printStats(ctx);
    }

    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    // Build a flat view in execution order for the conflict check and init loop.
    std::vector<System*> flat;
    for (auto& stage : m_systemsByStage) {
        for (auto& system : stage) flat.push_back(system.get());
    }

    // Validate system access declarations for write-write conflicts.
    for (size_t i = 0; i < flat.size(); ++i) {
        auto accessA = flat[i]->declareAccess();
        for (size_t j = i + 1; j < flat.size(); ++j) {
            auto accessB = flat[j]->declareAccess();
            for (TypeId wa : accessA.writes) {
                if (std::find(accessB.writes.begin(), accessB.writes.end(), wa) != accessB.writes.end()) {
                    LOG_WARNING("Systems %zu and %zu both write to component type %u", i, j, wa);
                }
            }
        }
    }

    for (System* system : flat) {
        system->init(ctx);
    }
    m_initialized = true;
}

void Engine::shutdownSystems() {
    // Reverse stage order, then reverse within each stage.
    for (auto stageIt = m_systemsByStage.rbegin(); stageIt != m_systemsByStage.rend(); ++stageIt) {
        for (auto it = stageIt->rbegin(); it != stageIt->rend(); ++it) {
            (*it)->shutdown();
        }
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
