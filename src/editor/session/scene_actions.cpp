#include "session/scene_actions.h"

#include <cstdint>

#include <imgui.h>

#include "core/clock.h"
#include "core/system.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "session/play_session.h"
#include "session/project/project_controller.h"
#include "session/scene_session.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

void SceneActions::resolve(EditorContext& ec, ScriptModule& scriptModule) {
    if (m_kind == Kind::None) return;
    if (m_stage == Stage::Save) {
        // A save inside a session is refused, and the save is for the authored scene Stop puts back.
        ec.session.stopPlaySession(ec.frame, ec.state);
        ec.session.save(ec.frame, ec.state);
        m_stage = Stage::Saving;
    }
    const std::optional<Stage> stage = next(m_stage, ec.state.sceneDirty, ec.session.isSaveDialogActive());
    if (!stage) {
        clear();
        return;
    }
    m_stage = *stage;
    if (m_stage != Stage::Run) return;

    const Kind kind = m_kind;
    const std::string target = m_target;
    const bool paused = m_pausedGame;
    const uint64_t world = ec.frame.scene.epoch();
    clear();
    perform(ec, scriptModule, kind, target);
    // One that left the session and its world alone - a project that would not open - gives the game its
    // pause back.
    if (paused && ec.play.isPlaying() && ec.frame.scene.epoch() == world) ec.frame.clock.setPaused(false);
}

void SceneActions::pauseForResolve(EditorContext& ec) {
    const bool prompting = m_stage == Stage::Ask && ec.state.sceneDirty;
    if (m_kind == Kind::None || prompting || !ec.play.isPlaying() || ec.frame.clock.isPaused()) return;
    ec.frame.clock.setPaused(true);
    m_pausedGame = true;
}

void SceneActions::perform(
    EditorContext& ec,
    ScriptModule& scriptModule,
    Kind kind,
    const std::string& target
) {
    switch (kind) {
        case Kind::Quit:
            ec.frame.window.requestClose();
            break;
        case Kind::New:
            ec.session.newScene(ec.frame, ec.state);
            break;
        case Kind::Open:
            ec.session.loadPath(ec.frame, ec.state, target);
            break;
        case Kind::OpenProject:
            ProjectController::open(ec, scriptModule, target, ProjectController::OpenKind::Requested);
            break;
        case Kind::None:
            break;
    }
}

void SceneActions::drawDialog(EditorContext& ec) {
    bool want = m_kind != Kind::None && m_stage == Stage::Ask;

    if (beginDialog("Unsaved Changes", want)) {
        ImGui::TextUnformatted("This scene has unsaved changes.");
        ImGui::Spacing();
        const std::string& path = ec.session.path();
        ImGui::TextDisabled("%s", path.empty() ? "(untitled scene)" : path.c_str());

        switch (dialogButtons(want, "Save", "Don't Save")) {
            case DialogResult::Confirm:
                m_stage = Stage::Save;
                break;
            case DialogResult::Alt:
                // Given up, so the copy kept for a crash goes too.
                ec.session.discardRecovery();
                m_stage = Stage::Run;
                break;
            case DialogResult::Cancel:
                clear();
                break;
            default: break;
        }
        endDialog();
    }

    // Dismissed unanswered: dropped, or the prompt reopens with no way out.
    if (!want && m_kind != Kind::None && m_stage == Stage::Ask) clear();
}

} // namespace Vkm::Engine
