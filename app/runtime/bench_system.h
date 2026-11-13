#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include "core/graphics.h"
#include "core/system.h"

namespace Vkm::App {

/**
 * @brief Where the camera stands and what it looks at.
 */
struct BenchKey {
    glm::vec3 position{0.0f};
    glm::vec3 lookAt{0.0f};
};

/**
 * @brief A place to hold the camera while frames are timed, or a lap to walk it round.
 */
struct BenchView {
    std::string              name;
    std::optional<glm::vec3> position;           ///< None keeps the game's own camera.
    glm::vec3                lookAt{0.0f};
    std::optional<float>     sunElevation;       ///< Degrees; none keeps the scene's.
    std::vector<BenchKey>    path;               ///< Keys to walk round, back to the first; none holds still.
    float                    lapSeconds = 0.0f;  ///< How long a lap takes, each key an equal share of it.
};

/**
 * @brief A quality tier to time, and what each view may cost at it.
 */
struct BenchTier {
    Engine::GraphicsPreset preset    = Engine::GraphicsPreset::Max;
    float                  averageMs = 0.0f;
    float                  low1Ms    = 0.0f;  ///< The slowest 1% of frames, averaged.
};

/**
 * @brief A project's bench.json (docs/reference/building.md).
 */
struct BenchPlan {
    float                  warmupSeconds = 10.0f;
    float                  seconds       = 20.0f;
    std::vector<BenchTier> tiers;
    std::vector<BenchView> views;
    std::filesystem::path  resultsDir;  ///< logs/bench beside the plan.
};

/**
 * @brief Read a bench plan.
 *
 * @param file The plan.
 * @param out  Filled when it is whole.
 * @return False when it is not, having said why.
 */
bool loadBenchPlan(const std::filesystem::path& file, BenchPlan& out);

/**
 * @brief `vkm_runtime --bench`: times every view at every tier, says which fit their budgets, and
 *        closes the window.
 *
 * The game runs as it always does; only the camera is set. Runs after the behaviors, so that wins
 * over a game's camera. Frames count once the splash has gone, the world's bakes are done and the
 * warm-up is over.
 */
class BenchSystem final : public Engine::System {
    public:
        explicit BenchSystem(BenchPlan plan) : m_plan(std::move(plan)) {}
        ~BenchSystem() override = default;

        BenchSystem(const BenchSystem& other) = delete;
        BenchSystem& operator=(const BenchSystem& other) = delete;

        BenchSystem(BenchSystem && other) = delete;
        BenchSystem& operator=(BenchSystem && other) = delete;

    public:
        void update(Engine::FrameContext& ctx) override;

        /// A run closed before its last view says so: it wrote no report, and it fails.
        void shutdown() override;

        /// Whether the run finished, its report written, with every view inside its budget.
        bool passed() const { return m_phase == Phase::Finished && m_passed; }

    private:
        /**
         * @brief Where the bench is in its plan.
         */
        enum class Phase {
            Waiting,   ///< Before its first frame.
            Timing,    ///< A run under way.
            Between,   ///< A run ended last frame; the next begins this one.
            Finished   ///< Every run done and the report written.
        };

    private:
        void beginRun(Engine::FrameContext& ctx);
        void endRun(Engine::FrameContext& ctx);
        void finish(Engine::FrameContext& ctx);

    private:
        BenchPlan m_plan;

        size_t m_tier = 0;
        size_t m_view = 0;

        std::chrono::steady_clock::time_point m_last;
        float                                 m_seconds = 0.0f;  ///< Into this view's warm-up and frames.
        std::vector<float>                    m_frames;          ///< This view's, in milliseconds.

        Phase          m_phase    = Phase::Waiting;
        bool           m_passed   = true;
        float          m_sceneSun = 0.0f;
        nlohmann::json m_results  = nlohmann::json::array();
};

} // namespace Vkm::App
