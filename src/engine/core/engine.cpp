#include "core/engine.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <vector>

#include "logger.h"

#include "core/engine_config.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

namespace {

/// Greedy layer assignment: walk systems in registration order, drop each
/// one into the earliest layer where its access doesn't conflict with any
/// system already in that layer. Two accesses conflict when one's writes
/// overlap the other's reads or writes (read-only / read-only is fine).
///
/// Systems with empty SystemAccess are treated as "writes everything" -
/// they conflict with anyone and therefore always end up in their own
/// dedicated layer. This is the safe default: a system that hasn't
/// declared accesses (or whose work is genuinely conservative, like
/// EventSystem flushing user callbacks) never gets parallel-dispatched.
bool overlaps(const std::vector<TypeId>& a, const std::vector<TypeId>& b) {
    for (TypeId t : a)
        if (std::find(b.begin(), b.end(), t) != b.end()) return true;
    return false;
}

bool isConservative(const SystemAccess& a) {
    return a.reads.empty() && a.writes.empty();
}

bool conflicts(const SystemAccess& a, const SystemAccess& b) {
    // Conservative declarations conflict with everything (we don't know
    // what they touch). Pure read-only against pure read-only is fine.
    if (isConservative(a) || isConservative(b)) return true;
    if (overlaps(a.writes, b.writes)) return true;
    if (overlaps(a.writes, b.reads))  return true;
    if (overlaps(a.reads,  b.writes)) return true;
    return false;
}

} // namespace

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
            Config::FixedTimeStep,
            static_cast<uint32_t>(m_window.getWidth()),
            static_cast<uint32_t>(m_window.getHeight()),
            nullptr
        };

        if (!m_initialized) {
            initSystems(ctx);
        }

        accumulator = std::min(accumulator + deltaTime, Config::MaxFrameAccumulator);
        while (accumulator >= Config::FixedTimeStep) {
            for (auto& stage : m_systemsByStage) {
                for (auto& system : stage) {
                    if (system->isEnabled()) {
                        system->fixedUpdate(ctx);
                    }
                }
            }
            accumulator -= Config::FixedTimeStep;
        }

        for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
            updateStage(static_cast<SystemStage>(s), ctx);
        }

        if (!m_window.swapBuffers()) break;

        m_statistics.update();
        printStats(ctx);
    }

    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    // Compute the per-stage layer plan from each system's declared access.
    buildSchedule();

    // Init systems in registration order, top to bottom of every stage.
    for (auto& stage : m_systemsByStage) {
        for (auto& system : stage) system->init(ctx);
    }
    m_initialized = true;
}

void Engine::buildSchedule() {
    static constexpr const char* STAGE_NAMES[] = {
        "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
    };

    for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
        auto& stage = m_systemsByStage[s];
        StageSchedule& plan = m_schedule[s];
        plan.layers.clear();

        // Greedy: walk systems in registration order, drop into earliest
        // layer with no conflict. Empty layers handled by always adding to
        // the back when nothing fit (preserves registration order across
        // layers as a tiebreak).
        for (auto& sysPtr : stage) {
            ScheduledSystem entry;
            entry.system = sysPtr.get();
            entry.access = sysPtr->declareAccess();

            bool placed = false;
            for (auto& layer : plan.layers) {
                bool ok = true;
                for (const auto& other : layer) {
                    if (conflicts(entry.access, other.access)) { ok = false; break; }
                }
                if (ok) {
                    layer.push_back(std::move(entry));
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                plan.layers.push_back({ std::move(entry) });
            }
        }

        // Diagnostic: summarise the plan. Helps the user see what would
        // benefit from setParallelDispatch and what's serialized by a
        // conservative declaration.
        if (!stage.empty()) {
            size_t maxLayer = 0;
            for (const auto& layer : plan.layers) {
                if (layer.size() > maxLayer) maxLayer = layer.size();
            }
            LOG_INFO("Schedule[%s]: %zu system(s), %zu layer(s), max layer width %zu",
                STAGE_NAMES[s], stage.size(), plan.layers.size(), maxLayer);
        }
    }
}

void Engine::setParallelDispatch(SystemStage stage, bool enabled) {
    const size_t idx = static_cast<size_t>(stage);
    if (idx >= m_schedule.size()) return;
    m_schedule[idx].parallelDispatch = enabled;
    if (enabled) {
        LOG_INFO("Parallel dispatch enabled for stage %zu", idx);
    }
}

void Engine::updateStage(SystemStage stage, FrameContext& ctx) {
    const StageSchedule& plan = m_schedule[static_cast<size_t>(stage)];

    for (const auto& layer : plan.layers) {
        // Single-system layers always run inline - dispatch overhead would
        // dwarf the work. Parallel dispatch needs the caller's explicit
        // opt-in (off by default) AND a layer with more than one entry.
        if (layer.size() == 1 || !plan.parallelDispatch) {
            for (const auto& entry : layer) {
                if (entry.system->isEnabled()) entry.system->update(ctx);
            }
            continue;
        }

        // Fan out across the layer via the engine's parallelFor pattern
        // (grain = 1, one system per task). parallelFor pumps the first
        // chunk on the main thread and waits on the rest before returning,
        // so the next layer sees every prior write before it starts.
        parallelFor(layer.size(), size_t{1}, [&](size_t i) {
            const auto& entry = layer[i];
            if (entry.system->isEnabled()) entry.system->update(ctx);
        });
    }
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
