#include "panels/project_settings_panel.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>

#include "core/system.h"
#include "editor_state.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "editor_actions.h"
#include "editor_context.h"
#include "loader/font_baker.h"
#include "ui/editor_icons.h"

#include "core/engine_config.h"
#include "core/clock.h"
#include "import/import_formats.h"
#include "io/project.h"
#include "system/loading/loading_system.h"
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

/**
 * @brief A path row: the project-relative path, a picker beside it, and a word when no file is
 *        there or it is not of the kind the row takes.
 *
 * @param label  The row's label.
 * @param path   Edited.
 * @param kinds  The extensions the row takes, lowercased.
 * @param what   Those kinds in words, for the warning.
 * @param picked Whether the Pick button was pressed.
 * @return Whether the path was typed over.
 */
bool pathRow(
    const char* label,
    std::string& path,
    const std::vector<std::string>& kinds,
    const char* what,
    bool& picked
) {
    const bool typed = propRow(label, nullptr, [&] {
        const float button = ImGui::GetFrameHeight() * 2.4f;
        ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
        const bool changed = ImGui::InputText("##path", &path);
        ImGui::SameLine();
        picked = ImGui::Button("Pick...", ImVec2(button, 0.0f));
        return changed;
    });
    std::error_code ec;
    if (path.empty()) return typed;
    const std::string extension = lowercaseExtension(path);
    if (std::find(kinds.begin(), kinds.end(), extension) == kinds.end()) {
        ImGui::TextColored(EditorStyle::WARNING, "Not %s: it is skipped.", what);
    } else if (!std::filesystem::exists(ProjectPaths::projectRoot() / path, ec)) {
        ImGui::TextColored(EditorStyle::WARNING, "No file there.");
    }
    return typed;
}

/// The images a logo can be, as the pickers spell extensions.
std::vector<std::string> imageKinds() { return {IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end()}; }

/// Up, down and remove for entry @p i of @p list: whether anything moved.
template<typename Entry>
bool entryControls(std::vector<Entry>& list, size_t i) {
    const float side = ImGui::GetFrameHeight();
    bool moved = false;
    if (iconButton("up", EditorIcon::ArrowUp, false, i > 0, "Earlier", side) && i > 0) {
        std::swap(list[i - 1], list[i]);
        moved = true;
    }
    ImGui::SameLine();
    const bool last = i + 1 >= list.size();
    if (iconButton("down", EditorIcon::ArrowDown, false, !last, "Later", side) && !last) {
        std::swap(list[i], list[i + 1]);
        moved = true;
    }
    ImGui::SameLine();
    if (iconButton("remove", EditorIcon::Trash, false, true, "Remove", side)) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        moved = true;
    }
    return moved;
}

/// The names that reach the file: a trailing unnamed row is not one.
std::vector<std::string> namedLayers(const Project& project) {
    std::vector<std::string> names = project.physicsLayers;
    while (!names.empty() && names.back().empty()) names.pop_back();
    return names;
}

/// One row per layer named, and one more to name the next.
void drawLayerNames(std::vector<std::string>& names) {
    const size_t rows = std::min(names.size() + 1, Project::MAX_PHYSICS_LAYERS);
    for (size_t bit = 0; bit < rows; ++bit) {
        std::string name = bit < names.size() ? names[bit] : std::string();
        const std::string label = physicsLayerName({}, static_cast<int>(bit));
        ImGui::PushID(static_cast<int>(bit));
        if (propString(label.c_str(), name)) {
            if (names.size() <= bit) names.resize(bit + 1);
            names[bit] = name;
        }
        ImGui::PopID();
    }
}

} // namespace

void ProjectSettingsPanel::drawSplash(Project& project) {
    ImGui::TextDisabled("Shown after the engine's logo, in this order.");
    const auto pick = [&](size_t index) {
        AssetPicker::Options options;
        options.title     = "Pick Logo";
        options.root      = ProjectPaths::projectRoot();
        options.recursive = true;
        options.extensions = imageKinds();
        options.hint      = "An image in the project, shown on black";
        m_imageFor        = index;
        m_imagePicker.open(options);
    };
    for (size_t i = 0; i < project.splash.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        SplashEntry& logo = project.splash[i];
        bool picked = false;
        const std::string label = "Logo " + std::to_string(i + 1);
        pathRow(label.c_str(), logo.image, imageKinds(), "an image (PNG, JPG, TGA or BMP)", picked);
        if (picked) pick(i);
        const char* held = "Held at full opacity; the fades add to it";
        propDrag("Seconds", &logo.seconds, 0.05f, 0.1f, 10.0f, "%.2f s", held);
        const bool moved = entryControls(project.splash, i);
        ImGui::PopID();
        if (moved) break;
    }
    if (ImGui::Button("+ Add Logo")) pick(project.splash.size());

    std::string picked;
    if (m_imagePicker.draw(picked)) {
        if (m_imageFor >= project.splash.size()) project.splash.push_back(SplashEntry{picked});
        else                                     project.splash[m_imageFor].image = picked;
    }
}

void ProjectSettingsPanel::drawFonts(Project& project) {
    ImGui::TextDisabled("A UI Text draws in one by naming it. Baked as the project is saved.");
    const auto pick = [&](size_t index) {
        AssetPicker::Options options;
        options.title     = "Pick Font";
        options.root      = ProjectPaths::projectRoot();
        options.recursive = true;
        options.extensions = {".ttf"};
        options.hint      = "A TrueType font in the project";
        m_fontFor         = index;
        m_fontPicker.open(options);
    };
    for (size_t i = 0; i < project.fonts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        FontEntry& font = project.fonts[i];
        propString("Name", font.name, "What a UI Text's Font names it by");
        bool picked = false;
        pathRow("File", font.file, {".ttf"}, "a TrueType font (.ttf)", picked);
        if (picked) pick(i);
        if (font.name.empty()) ImGui::TextColored(EditorStyle::WARNING, "Unnamed: no text can draw in it.");
        const bool moved = entryControls(project.fonts, i);
        ImGui::PopID();
        if (moved) break;
        ImGui::Separator();
    }
    if (ImGui::Button("+ Add Font")) pick(project.fonts.size());

    std::string picked;
    if (m_fontPicker.draw(picked)) {
        if (m_fontFor >= project.fonts.size()) {
            const std::string name = std::filesystem::path(picked).stem().string();
            project.fonts.push_back(FontEntry{name, picked});
        } else {
            project.fonts[m_fontFor].file = picked;
        }
    }
}

void ProjectSettingsPanel::drawLoading(Project& project) {
    LoadingScreen& loading = project.loading;
    ImGui::TextDisabled("Shown under the bar in turn, from one at random.");
    for (size_t i = 0; i < loading.tips.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = "Tip " + std::to_string(i + 1);
        propString(label.c_str(), loading.tips[i], "One line; an empty one is skipped");
        const bool moved = entryControls(loading.tips, i);
        ImGui::PopID();
        if (moved) break;
    }
    if (ImGui::Button("+ Add Tip")) loading.tips.emplace_back();

    ImGui::Spacing();
    bool picked = false;
    pathRow("Screen", loading.screen, {EditorActions::PREFAB_EXTENSION}, "a prefab", picked);
    if (picked) {
        AssetPicker::Options options;
        options.title      = "Pick Loading Screen";
        options.root       = ProjectPaths::prefabs();
        options.recursive  = true;
        options.extensions = {EditorActions::PREFAB_EXTENSION};
        options.hint       = "A prefab of UI canvases, drawn over the water and under the bar";
        m_screenPicker.open(options);
    }
    std::string chosen;
    if (m_screenPicker.draw(chosen)) loading.screen = chosen;
    propCheckbox("Water", &loading.water, "The engine's water behind it; off, plain black");
}

bool ProjectSettingsPanel::sameEditedFields(const Project& a, const Project& b) {
    return a.name        == b.name
        && a.description == b.description
        && a.version     == b.version
        && a.entryScene  == b.entryScene
        && a.tickRate    == b.tickRate
        && a.maxPlayers  == b.maxPlayers
        && a.netPort     == b.netPort
        && a.splash      == b.splash
        && a.fonts       == b.fonts
        && a.loading     == b.loading
        && namedLayers(a) == namedLayers(b);
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
        sectionLabel("On the wire");
        drawWireSchema();
    }
    endComponentCard();

    if (beginComponentCard("Startup Logos", EditorStyle::Accent::PREFAB, false)) drawSplash(project);
    endComponentCard();

    if (beginComponentCard("Fonts", EditorStyle::Accent::PREFAB, false)) drawFonts(project);
    endComponentCard();

    if (beginComponentCard("Loading Screen", EditorStyle::Accent::PREFAB, false)) drawLoading(project);
    endComponentCard();

    if (beginComponentCard("Physics Layers", EditorStyle::Accent::PHYSICS, false)) {
        ImGui::TextDisabled("Names for the Layer and Collides With boxes; bodies hold bits.");
        drawLayerNames(project.physicsLayers);
    }
    endComponentCard();

    ImGui::Spacing();

    const bool unsaved = !sameEditedFields(project, m_saved);
    if (unsaved) {
        ImGui::TextColored(EditorStyle::WARNING, "Unsaved");
        ImGui::SameLine();
        ImGui::TextDisabled("- a runtime reads the file, not this window");
    } else {
        ImGui::TextDisabled("Saved. The render settings save themselves as they are edited.");
    }

    // No root means no file to write and no directory to guess at.
    ImGui::BeginDisabled(root.empty());
    if (ImGui::Button("Save project.json", ImVec2(-FLT_MIN, EditorStyle::px(30.0f)))) {
        project.engineVersion = APP_VERSION;
        // The project's settings, never a game's tier (FrameContext::projectRender).
        project.render = ec.frame.projectRender;
        if (saveProject(ProjectPaths::projectRoot(), project)) {
            // At save, not at the edit: the ticking rate and the file's then agree.
            ec.frame.clock.setTickRate(project.tickRate);
            if (project.fonts != m_saved.fonts) {
                const float pixels = uiFontPixels(ec.frame.window.displayHeight());
                bakeProjectFonts(ec.frame.resources, project.fonts, pixels);
            }
            ec.loadingSystem.setScreen(project.loading);
            m_saved = project;
            state.pushToast(ToastKind::Info, "Project saved");
        } else {
            state.pushToast(ToastKind::Error, "Could not write project.json");
        }
    }
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace Vkm::Engine
