#include "panels/project_settings_panel.h"

#include <cfloat>
#include <filesystem>
#include <system_error>

#include <imgui.h>

#include "core/system.h"
#include "editor_state.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "editor_context.h"

#include "core/engine_config.h"
#include "core/clock.h"
#include "io/project.h"
#include "system/render/render_system.h"
#include "io/project_paths.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

// The ceiling loadProject clamps a hand-authored project.json to.
constexpr int MAX_SEATS = static_cast<int>(Config::MAX_PLAYERS_PER_GAME);

/**
 * @brief Ports below this are well-known; binding one needs privileges a game should not ask for.
 */
constexpr int FIRST_FREE_PORT = 1024;

void drawWireSchema() {
    const NetSchema& schema = NetSchema::get();

    if (schema.size() == 0) {
        ImGui::TextColored(EditorStyle::WARNING, "Nothing is replicated.");
        ImGui::TextWrapped(
            "A project says what goes on the wire from its vkmSetupNetwork "
            "entry, and the engine's own rows are registered from there too. "
            "With no module loaded, or none with that entry, nothing "
            "replicates and a game cannot be played over a wire at all."
        );
        return;
    }

    // The pair a refusal prints: the two ends compare exactly this.
    for (const NetType& type : schema.types()) ImGui::BulletText("%s", type.name.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Fingerprint %08x", schema.fingerprint());
}

/**
 * @brief The entry scene row: the path, a picker beside it, and whether it resolves.
 *
 * Empty does not read as a failure: a module's vkmBuildScene may build the world in code.
 *
 * @param project Project whose entryScene is edited.
 * @param picker Opened by the button.
 */
void drawEntryScene(Project& project, AssetPicker& picker) {
    propRow("Entry Scene", "Scene the runtime and the server boot, relative to the project root", [&] {
        const float button = ImGui::GetFrameHeight() * 2.4f;
        ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));

        const bool changed = ImGui::InputText("##entryScene", &project.entryScene);

        ImGui::SameLine();
        if (ImGui::Button("Pick...", ImVec2(button, 0.0f))) {
            AssetPicker::Options options;
            options.title      = "Pick Entry Scene";
            options.root       = ProjectPaths::scenes();
            options.extensions = {".json"};
            options.hint       = "The scene a runtime boots into.";
            picker.open(options);
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
    return a.name        == b.name
        && a.description == b.description
        && a.version     == b.version
        && a.entryScene  == b.entryScene
        && a.tickRate    == b.tickRate
        && a.maxPlayers  == b.maxPlayers
        && a.netPort     == b.netPort;
}

void ProjectSettingsPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    const ImVec2 size(EditorStyle::px(400.0f), EditorStyle::px(440.0f));
    if (!beginToolWindow("Project Settings", state.showProjectSettings, size)) return;

    Project& project = state.project;

    // The last-written copy belongs to the open project, not the one open at first draw.
    const std::string root = ProjectPaths::projectRoot().string();
    if (root != m_savedRoot) {
        m_savedRoot = root;
        m_saved     = project;
    }

    ImGui::TextDisabled("%s", root.empty() ? "No project is open." : root.c_str());
    ImGui::Spacing();

    if (beginComponentCard("Project", EditorStyle::Accent::PREFAB, true)) {
        propString("Name", project.name, "Titles the window and names the project everywhere");
        propString("Description", project.description, "One line on what it is, shown on the start screen");
        propString("Version", project.version, "The game's own version, which names its packages");
        drawEntryScene(project, m_scenePicker);

        int tickRate = static_cast<int>(project.tickRate);
        const bool tickRateEdited = propDragInt(
            "Tick Rate",
            &tickRate,
            1.0f,
            static_cast<int>(Config::MIN_TICK_RATE),
            static_cast<int>(Config::MAX_TICK_RATE),
            "Simulation ticks a second. Both ends of a game run at this rate"
        );
        if (tickRateEdited) {
            project.tickRate = static_cast<uint32_t>(tickRate);
        }
        ImGui::TextDisabled("%.1f ms a tick", 1000.0f / static_cast<float>(project.tickRate));

        // Read-only: only an engine writing the file changes it.
        propRow("Engine Version", "The engine that last wrote this file", [&] {
            const std::string& version = project.engineVersion;
            ImGui::TextDisabled("%s", version.empty() ? "unrecorded" : version.c_str());
            return false;
        });
        if (project.engineVersion != APP_VERSION) {
            ImGui::TextColored(EditorStyle::WARNING, "Saving stamps it %s.", APP_VERSION);
        }
    }
    endComponentCard();

    if (beginComponentCard("Multiplayer", EditorStyle::Accent::PHYSICS, true)) {
        int seats = static_cast<int>(project.maxPlayers);
        const bool seatsEdited = propDragInt(
            "Max Players",
            &seats,
            0.25f,
            0,
            MAX_SEATS,
            "Seats this game has. A connection past the last one is refused"
        );
        if (seatsEdited) {
            project.maxPlayers = static_cast<uint32_t>(seats);
        }

        int port = static_cast<int>(project.netPort);
        const bool portEdited = propDragInt(
            "Port",
            &port,
            4.0f,
            FIRST_FREE_PORT,
            65535,
            "Where the game is served unless a run passes --port"
        );
        if (portEdited) {
            project.netPort = static_cast<uint16_t>(port);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("On the wire");
        drawWireSchema();
    }
    endComponentCard();

    ImGui::Spacing();

    const bool unsaved = !sameEditedFields(project, m_saved);
    if (unsaved) {
        ImGui::TextColored(EditorStyle::WARNING, "Unsaved");
        ImGui::SameLine();
        ImGui::TextDisabled("- a runtime reads the file, not this window");
    } else {
        ImGui::TextDisabled("Saved. Save again to stamp the render settings as they are now.");
    }

    // No root means no file to write and no directory to guess at.
    ImGui::BeginDisabled(root.empty());
    if (ImGui::Button("Save project.json", ImVec2(-FLT_MIN, EditorStyle::px(30.0f)))) {
        project.engineVersion = APP_VERSION;
        // Taken live: RenderSettingsPanel edits the engine's settings, so the loaded copy is stale.
        project.render = ec.frame.render;
        if (saveProject(ProjectPaths::projectRoot(), project)) {
            // At save, not at the edit: the ticking rate and the file's then agree.
            ec.frame.clock.setTickRate(project.tickRate);
            m_saved = project;
            state.pushToast(ToastKind::Info, "Project saved");
        } else {
            state.pushToast(ToastKind::Error, "Could not write project.json; see the log");
        }
    }
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace Vkm::Engine
