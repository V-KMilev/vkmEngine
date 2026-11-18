#pragma once

#include <filesystem>
#include <optional>

namespace Vkm::Engine {

struct EditorContext;
class BehaviorSystem;
class ScriptModule;

/**
 * @brief Reloads what a build rewrites: the gameplay module, the shaders and the grading strip.
 *
 * The module is watched through the stamp its build writes once linked (stampOf), so a change seen
 * is a whole library. Polled, as a stat a frame does what a per-platform watcher would.
 */
class ReloadWatcher {
    public:
        /**
         * @brief What one look at the module's build stamp found.
         */
        struct Look {
            std::filesystem::path                          module;
            std::optional<std::filesystem::file_time_type> built;  ///< None while there is no stamp.
        };

        ReloadWatcher(ScriptModule& scriptModule, BehaviorSystem& behaviors);
        ~ReloadWatcher() = default;

        ReloadWatcher(const ReloadWatcher& other) = delete;
        ReloadWatcher& operator=(const ReloadWatcher& other) = delete;

        ReloadWatcher(ReloadWatcher && other) = delete;
        ReloadWatcher& operator=(ReloadWatcher && other) = delete;

    public:
        /**
         * @brief Reload the module when a build landed or File > Reload Scripts asked, and poll the shaders.
         *
         * Call each frame before the ImGui frame opens: a reload in a session stops it and plays again.
         */
        void update(EditorContext& ec);

        /**
         * @brief Whether a build of @p module landed since @p last: its stamp moved, or appeared.
         *
         * A first look at a module sets what unchanged is, so opening a project does not read as a build.
         *
         * @param built The stamp's write time now; none while there is no stamp.
         */
        static bool rebuilt(
            const Look& last,
            const std::filesystem::path& module,
            const std::optional<std::filesystem::file_time_type>& built
        ) {
            return last.module == module && built && built != last.built;
        }

        /// The stamp a build of @p module writes once it is linked.
        static std::filesystem::path stampOf(const std::filesystem::path& module) {
            return module.parent_path() / "game.built";
        }

    private:
        static constexpr float SHADER_POLL_INTERVAL = 1.0f;  ///< Seconds between shader and grading scans.

    private:
        /**
         * @brief Poll the shader sources and the grading strip, and reload what changed.
         *
         * Editor-only; a shipped runtime has no shader sources.
         */
        void pollShaders(EditorContext& ec);

        /**
         * @brief Swap in the module's new build, stopping and playing again a session that runs.
         *
         * A refused snapshot, a failed Stop or a failed load says so and keeps the old module.
         */
        void reload(EditorContext& ec);

    private:
        ScriptModule&   m_scriptModule;
        BehaviorSystem& m_behaviors;

        float m_shaderPollTimer = 0.0f;
        Look  m_last;  ///< The module last looked at and its stamp; another project's is a first look.
};

} // namespace Vkm::Engine
