#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Vkm::Engine {

struct EditorContext;
class ScriptModule;

/**
 * @brief The guarded way in for Quit, New Scene, Open Scene and Open Project, which throw the scene away.
 *
 * A request is parked, so no asker knows about unsaved changes and no scene is rebuilt in the ImGui
 * frame that asked.
 */
class SceneActions {
    public:
        /**
         * @brief The actions that throw the live scene away: quit, a new scene, open a scene, open a project.
         */
        enum class Kind : uint8_t { None, Quit, New, Open, OpenProject };

        /**
         * @brief How far the guard has got: unanswered, Save chosen, Save asked and not yet written,
         *        or cleared.
         */
        enum class Stage : uint8_t { Ask, Save, Saving, Run };

        SceneActions() = default;
        ~SceneActions() = default;

        SceneActions(const SceneActions& other) = delete;
        SceneActions& operator=(const SceneActions& other) = delete;

        SceneActions(SceneActions && other) = delete;
        SceneActions& operator=(SceneActions && other) = delete;

    public:
        /**
         * @brief Ask for @p kind; ignored while an earlier request is unresolved, so prompts cannot stack.
         *
         * @param target The scene file for Open, the project root for OpenProject, else empty.
         */
        void request(Kind kind, std::string target = {}) {
            if (m_kind != Kind::None) return;
            m_kind   = kind;
            m_stage  = Stage::Ask;
            m_target = std::move(target);
        }

        /**
         * @brief Carry the request a stage on, and perform it once the guard has cleared it.
         *
         * Call before the ImGui frame opens: the world may not be rebuilt with a window on the ImGui stack.
         */
        void resolve(EditorContext& ec, ScriptModule& scriptModule);

        /// Unsaved Changes, while a dirty scene's request waits: records the answer, acts on none.
        void drawDialog(EditorContext& ec);

        /**
         * @brief Pause a running game while a request waits for resolve(), which ends its session.
         *
         * The game's systems run before the editor's next frame: paused, they tick no more once the
         * request is asked or answered. One waiting on Unsaved Changes pauses nothing, as Cancel keeps
         * the session; one that ends none after all (a project that will not open) unpauses it again.
         * Call once nothing more in the frame can ask.
         */
        void pauseForResolve(EditorContext& ec);

        /**
         * @brief Where the guard goes from @p stage.
         *
         * @param dirty      Whether the scene has unsaved changes.
         * @param saveAsOpen Whether the Save-As a Save opened is still up.
         * @return The next stage; none when backing out of that Save-As withdrew the request.
         */
        static std::optional<Stage> next(Stage stage, bool dirty, bool saveAsOpen) {
            if (!dirty) return Stage::Run;
            if (stage == Stage::Saving && !saveAsOpen) return std::nullopt;
            return stage;
        }

        Kind pending() const { return m_kind; }

    private:
        /**
         * @brief Do the thing the guard cleared.
         *
         * @param kind   Which action was approved.
         * @param target Its target: a scene file, a project root, or empty.
         */
        void perform(EditorContext& ec, ScriptModule& scriptModule, Kind kind, const std::string& target);

        /// Drop the pending request, whatever stage it had reached.
        void clear() {
            m_kind       = Kind::None;
            m_stage      = Stage::Ask;
            m_pausedGame = false;
            m_target.clear();
        }

    private:
        Kind        m_kind       = Kind::None;
        Stage       m_stage      = Stage::Ask;
        bool        m_pausedGame = false;  ///< pauseForResolve paused the game for this request.
        std::string m_target;              ///< A scene file for Open, a project root for OpenProject, else empty.
};

} // namespace Vkm::Engine
