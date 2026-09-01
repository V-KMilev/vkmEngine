#include "panels/project_settings_panel.h"

#include <filesystem>

#include "framework/editor_common.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "framework/editor_context.h"

#include "core/engine_config.h"
#include "core/clock.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The seat count is a game's shape rather than a server's capacity, so the
 * ceiling is what an author could plausibly mean rather than what a machine could
 * carry.
 */
constexpr int MAX_SEATS = 64;

/**
 * @brief Below this the well-known ports live, and binding one needs privileges a game
 * should not be asking for.
 */
constexpr int FIRST_FREE_PORT = 1024;

void drawWireSchema() {
    const NetSchema& schema = NetSchema::get();

    if (schema.size() == 0) {
        ImGui::TextColored(EditorStyle::WARNING, "Nothing is replicated.");
        ImGui::TextWrapped("A project says what goes on the wire from its vkmSetupNetwork "
                           "entry, and the engine's own rows are registered from there too. "
                           "With no module loaded, or none with that entry, nothing "
                           "replicates and a game cannot be played over a wire at all.");
        return;
    }

    // The pair a refusal prints, shown where an author can read it before
    // launching rather than after a join has already failed: the two ends
    // compare exactly this, and a mismatch is diffing two lines.
    for (const NetType& type : schema.types()) {
        ImGui::BulletText("%s%s", type.name.c_str(),
                          type.policy == NetPolicy::OwnerOnly ? "  (owner only)" : "");
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Fingerprint %08x", schema.fingerprint());
}

} // namespace

void ProjectSettingsPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(EditorStyle::px(400.0f), EditorStyle::px(440.0f)),
                             ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Project Settings", &state.showProjectSettings,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    Project& project = state.project;

    if (beginComponentCard("Project", EditorStyle::Accent::Prefab, true)) {
        propString("Name", project.name, "Titles the window and names the project everywhere");
        propString("Entry Scene", project.entryScene,
                   "Scene the runtime and the server boot, relative to the project root");

        int tickRate = static_cast<int>(project.tickRate);
        if (propDragInt("Tick Rate", &tickRate, 1.0f,
                        static_cast<int>(Config::MIN_TICK_RATE),
                        static_cast<int>(Config::MAX_TICK_RATE),
                        "Simulation ticks a second. Both ends of a game run at this rate")) {
            project.tickRate = static_cast<uint32_t>(tickRate);
        }

        // Read-only: it records the engine that last wrote this file, and the
        // only thing that honestly changes it is another engine writing it.
        propRow("Engine Version", "The engine that last wrote this file", [&] {
            ImGui::TextDisabled("%s", project.engineVersion.empty() ? "unrecorded"
                                                                    : project.engineVersion.c_str());
            return false;
        });
        if (project.engineVersion != APP_VERSION) {
            ImGui::TextColored(EditorStyle::WARNING, "Saving stamps it %s.", APP_VERSION);
        }
    }
    endComponentCard();

    if (beginComponentCard("Multiplayer", EditorStyle::Accent::Physics, true)) {
        int seats = static_cast<int>(project.maxPlayers);
        if (propDragInt("Max Players", &seats, 0.25f, 0, MAX_SEATS,
                        "Seats this game has. A connection past the last one is refused")) {
            project.maxPlayers = static_cast<uint32_t>(seats);
        }

        int port = static_cast<int>(project.netPort);
        if (propDragInt("Port", &port, 4.0f, FIRST_FREE_PORT, 65535,
                        "Where the game is served unless a run passes --port")) {
            project.netPort = static_cast<uint16_t>(port);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("On the wire");
        drawWireSchema();
    }
    endComponentCard();

    ImGui::Spacing();

    // Written when someone says so, like every other file the editor writes.
    const std::filesystem::path root = ProjectPaths::projectRoot();
    ImGui::BeginDisabled(root.empty());
    if (ImGui::Button("Save project.json", ImVec2(-FLT_MIN, 0.0f))) {
        // Stamped with the engine doing the writing, which is what the field
        // records. The warning beside it says so before the button is pressed.
        project.engineVersion = APP_VERSION;
        if (saveProject(root, project)) {
            // Here rather than at the edit, because this is the moment the rate
            // the editor ticks at and the rate the file records agree.
            ec.frame.clock.setTickRate(project.tickRate);
            state.pushToast(EditorState::ToastKind::Info, "Project saved");
        } else {
            state.pushToast(EditorState::ToastKind::Error,
                            "Could not write project.json; see the log");
        }
    }
    ImGui::EndDisabled();

    // No root means no file to write and no directory to guess at. Nothing
    // reaches this today, but a save landing silently in the working directory
    // is the failure it costs one line to refuse.
    if (root.empty()) ImGui::TextDisabled("No project is open.");

    ImGui::End();
}

} // namespace Vkm::Engine
