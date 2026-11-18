#include "panels/project_settings_panel.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include "core/system.h"
#include "editor_state.h"
#include "ui/editor_dialogs.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/rows.h"
#include "actions/prefab_actions.h"
#include "editor_context.h"
#include "font_baker.h"
#include "ui/style/editor_icons.h"

#include "core/clock.h"
#include "import/import_formats.h"
#include "io/project/project.h"
#include "system/loading/loading_system.h"
#include "system/render/render_system.h"
#include "io/project/roots.h"
#include "net/wire/schema.h"
#include "vkm_platform/input/input_handle.h"
#include "core/input_map.h"
#include "io/project/actions_json.h"
#include "vkm_platform/window/window_manager.h"
#include "ui/physics_layer_widgets.h"

namespace Vkm::Engine {

namespace {

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
 * @param rows   The Project's rows; its entryScene is edited.
 * @param picker Opened by the button.
 */
void drawEntryScene(Rows<Project>& rows, FilePicker& picker) {
    rows.rowWith<&Project::entryScene>([&](std::string& scene, const FieldInfo&) {
        const float button = ImGui::GetFrameHeight() * 2.4f;
        ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
        const bool changed = ImGui::InputText("##entryScene", &scene);
        ImGui::SameLine();
        if (ImGui::Button("Pick...", ImVec2(button, 0.0f))) {
            FilePicker::Options options;
            options.title      = "Pick Entry Scene";
            options.root       = Roots::scenes();
            options.extensions = {".json"};
            options.hint       = "The scene a runtime boots into.";
            picker.open(options);
        }
        return changed;
    });

    Project& project = rows.value();
    std::string picked;
    if (picker.draw(picked)) project.entryScene = picked;

    if (project.entryScene.empty()) {
        cardNote(Note::Warning, "None: the game has no world; Save As names one.");
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(Roots::projectRoot() / project.entryScene, ec)) {
        cardNote(Note::Warning, "No file there: the game will not start.");
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
    } else if (!std::filesystem::exists(Roots::projectRoot() / path, ec)) {
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

/**
 * @brief A key or button down now that was not when @p held was last taken; @p held then takes
 *        what is down now.
 *
 * Levels, not strikes: the game's map has taken this frame's strikes.
 */
std::optional<InputBinding> newlyDown(const Platform::InputHandle& device, std::vector<bool>& held) {
    constexpr size_t KEYS    = static_cast<size_t>(Platform::Key::Count);
    constexpr size_t BUTTONS = static_cast<size_t>(Platform::MouseButton::Count);
    held.resize(KEYS + BUTTONS, false);
    std::optional<InputBinding> found;
    for (size_t at = 0; at < KEYS; ++at) {
        const auto key  = static_cast<Platform::Key>(at);
        const bool down = device.isKeyPressed(key);
        if (down && !held[at] && !found) found = InputBinding::key(key);
        held[at] = down;
    }
    for (size_t at = 0; at < BUTTONS; ++at) {
        const auto button = static_cast<Platform::MouseButton>(at);
        const bool down   = device.isButtonPressed(button);
        if (down && !held[KEYS + at] && !found) found = InputBinding::mouse(button);
        held[KEYS + at] = down;
    }
    return found;
}

} // namespace

void ProjectSettingsPanel::drawActions(Project& project, const Platform::InputHandle& device) {
    std::vector<InputAction>& actions = project.actions;
    const size_t room = MAX_INPUT_ACTIONS - 1;  // InputActions::UI_CLICK holds the first slot
    ImGui::TextDisabled(
        "%zu of %zu travel in a command; a click on a key takes it off.",
        std::min(actions.size(), room),
        room
    );

    size_t removed = SIZE_MAX;
    for (size_t i = 0; i < actions.size(); ++i) {
        InputAction& action = actions[i];
        ImGui::PushID(static_cast<int>(i));
        drawPropertyLabel(action.name.c_str());
        size_t unbound = SIZE_MAX;
        for (size_t b = 0; b < action.bindings.size(); ++b) {
            ImGui::PushID(static_cast<int>(b));
            if (ImGui::SmallButton(bindingName(action.bindings[b]).c_str())) unbound = b;
            if (ImGui::IsItemHovered() && action.bindings[b].scale != 1.0f) {
                ImGui::SetTooltip("Scale %g", action.bindings[b].scale);
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (unbound != SIZE_MAX) {
            action.bindings.erase(action.bindings.begin() + static_cast<std::ptrdiff_t>(unbound));
        }
        const bool listening = m_listeningFor == i;
        if (ImGui::SmallButton(listening ? "press a key..." : "+")) {
            m_listeningFor = listening ? SIZE_MAX : i;
            newlyDown(device, m_held);  // what is down now, the click among it, is no answer
        }
        ImGui::SameLine();
        if (removeButton("remove", "Remove this action")) removed = i;
        ImGui::PopID();
    }
    if (removed != SIZE_MAX) {
        actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(removed));
        m_listeningFor = SIZE_MAX;
    }

    if (m_listeningFor < actions.size()) {
        if (const std::optional<InputBinding> pressed = newlyDown(device, m_held)) {
            actions[m_listeningFor].bindings.push_back(*pressed);
            m_listeningFor = SIZE_MAX;
        }
    }

    ImGui::SetNextItemWidth(-EditorStyle::px(60.0f));
    ImGui::InputTextWithHint("##newAction", "New action, as Move/Forward", &m_newAction);
    ImGui::SameLine();
    const bool taken = std::any_of(actions.begin(), actions.end(), [&](const InputAction& action) {
        return action.name == m_newAction;
    });
    ImGui::BeginDisabled(m_newAction.empty() || taken);
    if (ImGui::Button("Add", ImVec2(-FLT_MIN, 0.0f))) {
        actions.push_back({m_newAction, {}});
        std::sort(actions.begin(), actions.end(), [](const InputAction& a, const InputAction& b) {
            return a.name < b.name;
        });
        m_newAction.clear();
    }
    ImGui::EndDisabled();
}

void ProjectSettingsPanel::drawSplash(Project& project) {
    ImGui::TextDisabled("Shown after the engine's logo, in this order.");
    const auto pick = [&](size_t index) {
        FilePicker::Options options;
        options.title      = "Pick Logo";
        options.root       = Roots::projectRoot();
        options.recursive  = true;
        options.extensions = imageKinds();
        options.hint       = "An image in the project, shown on black";
        m_imageFor         = index;
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
        FilePicker::Options options;
        options.title      = "Pick Font";
        options.root       = Roots::projectRoot();
        options.recursive  = true;
        options.extensions = {".ttf"};
        options.hint       = "A TrueType font in the project";
        m_fontFor          = index;
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
        FilePicker::Options options;
        options.title      = "Pick Loading Screen";
        options.root       = Roots::prefabs();
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
    // By the reflected fields, so a row added is a field compared; the render block saves itself.
    const auto same = [&](const auto& field) {
        if constexpr (std::is_same_v<std::decay_t<decltype(a.*(field.ptr))>, RenderSettings>) return true;
        else return a.*(field.ptr) == b.*(field.ptr);
    };
    const bool reflected = std::apply(
        [&](const auto&... field) { return (same(field) && ...); },
        Reflect::Traits<Project>::fields()
    );
    return reflected
        && a.splash  == b.splash
        && a.fonts   == b.fonts
        && a.loading == b.loading
        && actionsToJson(a.actions) == actionsToJson(b.actions)
        && namedLayers(a) == namedLayers(b);
}

void ProjectSettingsPanel::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    const ImVec2 size(EditorStyle::px(400.0f), EditorStyle::px(440.0f));
    if (!beginToolWindow("Project Settings", state.showProjectSettings, size)) return;

    Project& project = state.project;
    const std::string root = Roots::projectRoot().string();

    ImGui::TextDisabled("%s", root.empty() ? "No project is open." : root.c_str());
    ImGui::Spacing();

    Rows<Project> rows(project, {}, nullptr);
    if (beginComponentCard("Project", EditorIcon::None, true)) {
        rows.row<&Project::name>();
        rows.row<&Project::description>();
        rows.row<&Project::picture>();
        rows.row<&Project::version>();
        drawEntryScene(rows, m_scenePicker);
        rows.row<&Project::tickRate>();
        ImGui::TextDisabled("%.1f ms a tick", 1000.0f / static_cast<float>(project.tickRate));
        // Read-only: only an engine writing the file changes it.
        rows.rowWith<&Project::engineVersion>([](std::string& version, const FieldInfo&) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", version.empty() ? "unrecorded" : version.c_str());
            return false;
        });
        if (project.engineVersion != APP_VERSION) {
            ImGui::TextColored(EditorStyle::WARNING, "Saving stamps it %s.", APP_VERSION);
        }
    }
    endComponentCard();

    if (beginComponentCard("Multiplayer", EditorIcon::None, true)) {
        rows.row<&Project::maxPlayers>();
        rows.row<&Project::netPort>();
        ImGui::Spacing();
        sectionLabel("On the wire");
        drawWireSchema();
    }
    endComponentCard();

    if (beginComponentCard("Startup Logos", EditorIcon::None, false)) drawSplash(project);
    endComponentCard();

    if (beginComponentCard("Fonts", EditorIcon::None, false)) drawFonts(project);
    endComponentCard();

    if (beginComponentCard("Loading Screen", EditorIcon::None, false)) drawLoading(project);
    endComponentCard();

    if (beginComponentCard("Input", EditorIcon::None, false)) {
        drawActions(project, ec.frame.window.getInputHandle());
    }
    endComponentCard();

    if (beginComponentCard("Physics Layers", EditorIcon::None, false)) {
        ImGui::TextDisabled("Names for the Layer and Collides With boxes; bodies hold bits.");
        drawLayerNames(project.physicsLayers);
    }
    endComponentCard();

    ImGui::Spacing();

    const bool unsaved = !sameEditedFields(project, state.projectSaved);
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
        // The project's settings, never a game's tier or a preview's (Graphics::project).
        project.render = ec.frame.graphics.project();
        if (saveProject(Roots::projectRoot(), project)) {
            // At save, not at the edit: the ticking rate and the file's then agree.
            ec.frame.clock.setTickRate(project.tickRate);
            if (project.fonts != state.projectSaved.fonts) {
                const float pixels = uiFontPixels(ec.frame.window.displayHeight());
                bakeProjectFonts(ec.frame.resources, project.fonts, pixels);
            }
            ec.loadingSystem.setScreen(project.loading);
            ec.frame.input.setActions(project.actions);
            state.projectSaved = project;
            state.toasts.push(ToastKind::Info, "Project saved");
        } else {
            state.toasts.push(ToastKind::Error, "Could not write project.json");
        }
    }
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace Vkm::Engine
