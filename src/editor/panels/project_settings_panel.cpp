#include "panels/project_settings_panel.h"

#include <filesystem>
#include <system_error>

#include "framework/editor_common.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "framework/editor_context.h"

#include "core/engine_config.h"
#include "core/clock.h"
#include "io/project.h"
#include "system/render/render_system.h"
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

/**
 * @brief The entry scene row: the path, a picker beside it, and whether it resolves.
 *
 * Empty is not a failure and does not read as one. A project whose world is
 * built in code - which both shipped examples are - has nothing for this to
 * point at, and the module's vkmBuildScene is what fills the world instead.
 *
 * @param project Project whose entryScene is edited.
 * @param picker The panel's own picker, opened by the button.
 */
void drawEntryScene(Project& project, AssetPicker& picker) {
    propRow("Entry Scene", "Scene the runtime and the server boot, relative to the project root",
            [&] {
        const float button = ImGui::GetFrameHeight() * 2.4f;
        ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));

        const bool changed = ImGui::InputText("##entryScene", &project.entryScene);

        ImGui::SameLine();
        if (ImGui::Button("Pick...", ImVec2(button, 0.0f))) {
            picker.options().title      = "Pick Entry Scene";
            picker.options().root       = ProjectPaths::scenes();
            picker.options().recursive  = false;
            picker.options().extensions = {".json"};
            picker.options().relativeTo = ProjectPaths::projectRoot();
            picker.options().hint       = "The scene a runtime boots into.";
            picker.open();
        }
        return changed;
    });

    std::string picked;
    if (picker.draw(picked)) project.entryScene = picked;

    if (project.entryScene.empty()) {
        ImGui::TextDisabled("None: the world is built in code, from the module's vkmBuildScene.");
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(ProjectPaths::projectRoot() / project.entryScene, ec)) {
        ImGui::TextColored(EditorStyle::WARNING, "No file there. A runtime starts empty.");
    }
}

} // namespace

bool ProjectSettingsPanel::sameEditedFields(const Project& a, const Project& b) {
    return a.name       == b.name
        && a.entryScene == b.entryScene
        && a.tickRate   == b.tickRate
        && a.maxPlayers == b.maxPlayers
        && a.netPort    == b.netPort;
}

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

    // The window follows the open project, so what it last saw written belongs
    // to that project and not to whichever one was open when it first drew.
    const std::string root = ProjectPaths::projectRoot().string();
    if (root != m_savedRoot) {
        m_savedRoot = root;
        m_saved     = project;
    }

    // Where this is, said once: every other line here is a value, and a value
    // means something different in a project you did not think you had open.
    ImGui::TextDisabled("%s", root.empty() ? "No project is open." : root.c_str());
    ImGui::Spacing();

    if (beginComponentCard("Project", EditorStyle::Accent::Prefab, true)) {
        propString("Name", project.name, "Titles the window and names the project everywhere");
        drawEntryScene(project, m_scenePicker);

        int tickRate = static_cast<int>(project.tickRate);
        if (propDragInt("Tick Rate", &tickRate, 1.0f,
                        static_cast<int>(Config::MIN_TICK_RATE),
                        static_cast<int>(Config::MAX_TICK_RATE),
                        "Simulation ticks a second. Both ends of a game run at this rate")) {
            project.tickRate = static_cast<uint32_t>(tickRate);
        }
        // The rate as the thing a gameplay author actually feels: a fixed step
        // is what onFixedUpdate is handed and what a physics tick costs.
        ImGui::TextDisabled("%.1f ms a tick", 1000.0f / static_cast<float>(project.tickRate));

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

    // No root means no file to write and no directory to guess at. Nothing
    // reaches this today, but a save landing silently in the working directory
    // is the failure it costs one line to refuse.
    const bool unsaved = !sameEditedFields(project, m_saved);
    if (unsaved) {
        ImGui::TextColored(EditorStyle::WARNING, "Unsaved");
        ImGui::SameLine();
        ImGui::TextDisabled("- a runtime reads the file, not this window");
    } else {
        ImGui::TextDisabled("Saved. Save again to stamp the render settings as they are now.");
    }

    // Written when someone says so, like every other file the editor writes.
    ImGui::BeginDisabled(root.empty());
    if (ImGui::Button("Save project.json", ImVec2(-FLT_MIN, EditorStyle::px(30.0f)))) {
        // Stamped with the engine doing the writing, which is what the field
        // records. The warning beside it says so before the button is pressed.
        project.engineVersion = APP_VERSION;
        // Taken live: the Render Settings panel edits the render system
        // directly, so the copy loaded at open is stale the moment a slider
        // moves, and writing that back would undo the author's tuning.
        project.render = ec.renderSystem.getSettings();
        if (saveProject(ProjectPaths::projectRoot(), project)) {
            // Here rather than at the edit, because this is the moment the rate
            // the editor ticks at and the rate the file records agree.
            ec.frame.clock.setTickRate(project.tickRate);
            m_saved = project;
            state.pushToast(EditorState::ToastKind::Info, "Project saved");
        } else {
            state.pushToast(EditorState::ToastKind::Error,
                            "Could not write project.json; see the log");
        }
    }
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace Vkm::Engine
