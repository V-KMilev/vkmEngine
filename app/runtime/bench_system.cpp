#define VKM_LOG_CATEGORY "BENCH"

#include "app/runtime/bench_system.h"

#include <cmath>
#include <fstream>
#include <system_error>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "core/graphics.h"
#include "base/math/rotation.h"
#include "app/runtime/frame_stats.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/settings/environment.h"
#include "ecs/scene.h"
#include "io/json/json_file.h"
#include "io/json/json_number.h"
#include "io/json/json_vec.h"
#include "vkm_platform/window/window_manager.h"
#include "system/render/render_backend.h"
#include "core/frame/splash_frame.h"

namespace Vkm::App {

namespace {

using Json = nlohmann::json;

bool readVec3(const Json& view, const char* key, glm::vec3& out) {
    const auto found = view.find(key);
    if (found == view.end() || found->size() != 3) return false;
    out = Engine::detail::jsonToVec<3>(*found);
    return true;
}

float toFps(float ms) { return ms > 0.0f ? 1000.0f / ms : 0.0f; }

// Stand @p eye at @p place, which is the world's, in its parent's space: a camera may ride a rig.
void standAt(Engine::Scene& scene, Engine::EntityId eye, const BenchKey& place) {
    Engine::Transform* camera = scene.tryGet<Engine::Transform>(eye);
    if (!camera) return;
    float yaw   = 0.0f;
    float pitch = 0.0f;
    Engine::Math::toYawPitch(place.lookAt - place.position, yaw, pitch);
    Engine::Transform world;
    world.position = place.position;
    world.rotation = Engine::Math::fromYawPitch(yaw, pitch);

    const Engine::Hierarchy* node = scene.tryGet<Engine::Hierarchy>(eye);
    const glm::mat4 parent = node && scene.isAlive(node->parent)
        ? Engine::HierarchyOperations::computeWorldMatrix(scene, node->parent)
        : glm::mat4(1.0f);
    const Engine::Transform local = Engine::Transform::fromModelMatrix(
        glm::inverse(parent) * Engine::Transform::computeModelMatrix(world)
    );
    camera->position = local.position;
    camera->rotation = local.rotation;
}

} // namespace

bool loadBenchPlan(const std::filesystem::path& file, BenchPlan& out) {
    std::ifstream in(file);
    const Json doc = Json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        LOG_ERROR("%s is not a bench plan: it is missing or not JSON", file.string().c_str());
        return false;
    }

    out.warmupSeconds = Engine::detail::numberAt(doc, "warmupSeconds", out.warmupSeconds);
    out.seconds       = Engine::detail::numberAt(doc, "seconds", out.seconds);
    out.resultsDir    = file.parent_path() / "logs" / "bench";

    for (const Json& row : doc.value("tiers", Json::array())) {
        BenchTier tier;
        const std::string name = row.value("tier", "");
        if (!Engine::Reflect::enumFromNameChecked(name, tier.preset)) {
            LOG_ERROR("%s: '%s' is not a quality tier", file.string().c_str(), name.c_str());
            return false;
        }
        tier.averageMs = Engine::detail::numberAt(row, "averageMs", 0.0f);
        tier.low1Ms    = Engine::detail::numberAt(row, "low1Ms", 0.0f);
        // Every frame is over a budget of none: such a tier could only fail.
        if (tier.averageMs <= 0.0f || tier.low1Ms <= 0.0f) {
            LOG_ERROR(
                "%s: tier '%s' needs an averageMs and a low1Ms over 0",
                file.string().c_str(),
                name.c_str()
            );
            return false;
        }
        out.tiers.push_back(tier);
    }

    for (const Json& row : doc.value("views", Json::array())) {
        BenchView view;
        view.name = row.value("name", "");
        glm::vec3 position{0.0f};
        if (readVec3(row, "position", position)) {
            view.position = position;
            if (!readVec3(row, "lookAt", view.lookAt)) {
                LOG_ERROR(
                    "%s: view '%s' has a position and nothing to look at",
                    file.string().c_str(),
                    view.name.c_str()
                );
                return false;
            }
        }
        if (const auto sun = row.find("sunElevation"); sun != row.end()) {
            view.sunElevation = Engine::detail::jsonToNumber<float>(*sun);
        }
        for (const Json& key : row.value("path", Json::array())) {
            BenchKey place;
            if (!readVec3(key, "position", place.position) || !readVec3(key, "lookAt", place.lookAt)) {
                LOG_ERROR(
                    "%s: a key of view '%s' lacks a position or a lookAt",
                    file.string().c_str(),
                    view.name.c_str()
                );
                return false;
            }
            view.path.push_back(place);
        }
        view.lapSeconds = Engine::detail::numberAt(row, "lapSeconds", 0.0f);
        if (!view.path.empty() && view.lapSeconds <= 0.0f) {
            LOG_ERROR("%s: view '%s' has a path and no lapSeconds", file.string().c_str(), view.name.c_str());
            return false;
        }
        out.views.push_back(std::move(view));
    }

    if (out.tiers.empty() || out.views.empty()) {
        LOG_ERROR("%s names no tiers or no views", file.string().c_str());
        return false;
    }
    return true;
}

void BenchSystem::beginRun(Engine::FrameContext& ctx) {
    ctx.graphics.setQuality(m_plan.tiers[m_tier].preset);
    ctx.scene.environment().sky.sunElevation = m_plan.views[m_view].sunElevation.value_or(m_sceneSun);
    m_seconds = 0.0f;
    m_frames.clear();
}

void BenchSystem::endRun(Engine::FrameContext& ctx) {
    const BenchTier& tier = m_plan.tiers[m_tier];
    const BenchView& view = m_plan.views[m_view];
    const char* name = Engine::Reflect::enumName(tier.preset);
    const FrameStats stats = summarizeFrames(m_frames);

    const bool fits = stats.averageMs <= tier.averageMs && stats.low1Ms <= tier.low1Ms;
    m_passed = m_passed && fits;

    LOG_INFO(
        "%-6s %-10s %6.1f fps  %6.2f ms  1%% low %6.1f  0.1%% low %6.1f  worst %6.2f ms  spikes %4zu  %s",
        name,
        view.name.c_str(),
        toFps(stats.averageMs),
        stats.averageMs,
        toFps(stats.low1Ms),
        toFps(stats.low01Ms),
        stats.worstMs,
        stats.spikes,
        fits ? "fits" : "OVER BUDGET"
    );

    m_results.push_back({
        {"tier", name},
        {"view", view.name},
        {"width", ctx.window.getWidth()},
        {"height", ctx.window.getHeight()},
        {"frames", stats.frames},
        {"averageMs", stats.averageMs},
        {"medianMs", stats.medianMs},
        {"low1Ms", stats.low1Ms},
        {"low01Ms", stats.low01Ms},
        {"worstMs", stats.worstMs},
        {"spikes", stats.spikes},
        {"fits", fits},
    });

    // After the frames, so writing it costs none of them.
    ctx.graphics.saveScreenshot(
        (m_plan.resultsDir / (std::string(name) + "_" + view.name + ".png")).string()
    );
}

void BenchSystem::update(Engine::FrameContext& ctx) {
    if (m_phase == Phase::Finished) return;

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const float frameMs = std::chrono::duration<float, std::milli>(now - m_last).count();
    m_last = now;

    if (m_phase == Phase::Waiting) {
        m_phase    = Phase::Timing;
        m_sceneSun = ctx.scene.environment().sky.sunElevation;
        std::error_code ec;
        std::filesystem::create_directories(m_plan.resultsDir, ec);
        // Uncapped: a frame the monitor holds back says nothing about the frame.
        ctx.window.setVSync(false);
        beginRun(ctx);
        return;
    }

    // The next run begins a frame after the last ended, so the frame its screenshot holds is drawn
    // as that run drew, not under the next one's sun and tier.
    if (m_phase == Phase::Between) {
        m_phase = Phase::Timing;
        if (++m_view == m_plan.views.size()) {
            m_view = 0;
            ++m_tier;
        }
        if (m_tier == m_plan.tiers.size()) {
            finish(ctx);
            return;
        }
        beginRun(ctx);
    }

    const BenchView& view = m_plan.views[m_view];
    std::optional<BenchKey> place;
    if (view.position) place = BenchKey{*view.position, view.lookAt};
    if (!view.path.empty()) {
        const float keys  = static_cast<float>(view.path.size());
        const float along = std::fmod(m_seconds / view.lapSeconds, 1.0f) * keys;
        const size_t at = static_cast<size_t>(along) % view.path.size();
        const BenchKey& from = view.path[at];
        const BenchKey& to   = view.path[(at + 1) % view.path.size()];
        const float t = along - std::floor(along);
        place = BenchKey{glm::mix(from.position, to.position, t), glm::mix(from.lookAt, to.lookAt, t)};
    }
    if (place) standAt(ctx.scene, ctx.camera.of(ctx.scene), *place);

    // A bake is a loading screen and the ident is drawn over black: neither is a frame of the game.
    if (!ctx.readiness.ready()) return;
    if (ctx.splash && ctx.splash->isShowing()) return;

    m_seconds += frameMs / 1000.0f;
    if (m_seconds < m_plan.warmupSeconds) return;

    m_frames.push_back(frameMs);
    if (m_seconds < m_plan.warmupSeconds + m_plan.seconds) return;

    endRun(ctx);
    m_phase = Phase::Between;
}

void BenchSystem::shutdown() {
    if (m_phase == Phase::Finished) return;
    LOG_ERROR("The bench was closed before its last view: it wrote no report, and it fails");
}

void BenchSystem::finish(Engine::FrameContext& ctx) {
    m_phase = Phase::Finished;
    const std::filesystem::path report = m_plan.resultsDir / "report.json";
    // A run whose report is lost fails: nothing is left to compare. The write has said why.
    m_passed = Engine::detail::writeJsonFile(report, m_results, "Bench report") && m_passed;
    LOG_INFO("Bench %s: %s", m_passed ? "passed" : "FAILED", report.string().c_str());
    ctx.window.requestClose();
}

} // namespace Vkm::App
