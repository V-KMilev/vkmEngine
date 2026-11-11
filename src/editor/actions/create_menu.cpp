#include "actions/create_menu.h"

#include <cctype>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "actions/create_entity.h"
#include "actions/prefab_actions.h"
#include "editor_state.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {
namespace CreateMenu {

using EditorActions::CreateGroup;
using EditorActions::EntityKindRow;

namespace {

/// A prefab file as a menu reads it: "smoke_plume.json" is "Smoke Plume".
std::string effectLabel(const std::string& file) {
    std::string label = std::filesystem::path(file).stem().string();
    bool wordStart    = true;
    for (char& c : label) {
        if (c == '_') {
            c         = ' ';
            wordStart = true;
        } else if (wordStart) {
            c         = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            wordStart = false;
        }
    }
    return label;
}

} // namespace

void draw(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent) {
    if (!beginSubmenu(parent ? "Create Child" : "Create")) return;
    items(scene, resources, state, parent);
    ImGui::EndMenu();
}

void items(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent) {
    const auto group = [&](CreateGroup listed) {
        for (const EntityKindRow& row : EditorActions::entityKinds()) {
            if (row.group != listed) continue;
            if (listRow(row.label, row.icon, row.label, false).clicked) {
                const EntityId made = EditorActions::createEntity(scene, resources, state, row.kind, parent);
                if (made) state.selectEntity(made);
            }
            if (row.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.tip);
        }
    };
    const auto submenu = [&](const char* label, CreateGroup listed) {
        if (!beginSubmenu(label)) return;
        group(listed);
        ImGui::EndMenu();
    };

    group(CreateGroup::Top);
    ImGui::Separator();
    group(CreateGroup::Scene);
    ImGui::Separator();
    group(CreateGroup::Audio);
    ImGui::Separator();
    submenu("Light", CreateGroup::Light);
    if (!parent && beginSubmenu("Effect")) {
        for (const std::string& file : EditorActions::shippedEffects()) {
            if (listRow(file.c_str(), EditorIcon::Particle, effectLabel(file).c_str(), false).clicked) {
                EditorActions::placeEffect(scene, resources, state, file);
            }
        }
        ImGui::EndMenu();
    }
    submenu("Primitive", CreateGroup::Primitive);
    group(CreateGroup::Ground);
    submenu("Physics", CreateGroup::Physics);
    submenu("UI", CreateGroup::UI);
    // The menu closes on click, so the modals live in dialogs EditorSystem owns.
    if (!parent) {
        ImGui::Separator();
        if (listRow("Prefab", EditorIcon::Prefab, "Prefab", false).clicked) {
            state.requests.ask(Ask::PlacePrefab);
        }
        if (listRow("Import Model", EditorIcon::Import, "Import Model", false).clicked) {
            state.requests.ask(Ask::ImportModel);
        }
    }
}

} // namespace CreateMenu
} // namespace Vkm::Engine
