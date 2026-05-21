#include "framework/editor_settings.h"
#include "framework/editor_state.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "core/system.h"   // APP_ROOT_DIR (via compile_definitions, actually)

namespace Engine {
namespace EditorSettings {

namespace {
    using nlohmann::json;

    json keybindToJson(const KeyBind& k) {
        return json{ {"key", static_cast<int>(k.key)}, {"mods", k.mods} };
    }
    void keybindFromJson(const json& j, KeyBind& k) {
        k.key  = static_cast<ImGuiKey>(j.value("key",  static_cast<int>(ImGuiKey_None)));
        k.mods = static_cast<uint8_t>(j.value("mods", 0));
    }
}

std::string path() {
    return std::string(APP_ROOT_DIR) + "/editor_settings.json";
}

bool load(EditorState& state) {
    std::ifstream in(path());
    if (!in.good()) return false;
    json j;
    try { in >> j; } catch (...) { return false; }

    // Panel visibility
    state.showStats         = j.value("showStats",         state.showStats);
    state.showHierarchy     = j.value("showHierarchy",     state.showHierarchy);
    state.showInspector     = j.value("showInspector",     state.showInspector);
    state.showBottom        = j.value("showBottom",        state.showBottom);

    // Panel sizes
    state.leftPanelWidth    = j.value("leftPanelWidth",    state.leftPanelWidth);
    state.rightPanelWidth   = j.value("rightPanelWidth",   state.rightPanelWidth);
    state.bottomPanelHeight = j.value("bottomPanelHeight", state.bottomPanelHeight);

    // Gizmo defaults
    state.gizmoOperation = static_cast<GizmoOperation>(
        j.value("gizmoOperation", static_cast<int>(state.gizmoOperation)));
    state.gizmoMode = static_cast<GizmoMode>(
        j.value("gizmoMode", static_cast<int>(state.gizmoMode)));

    // Snap
    state.snapEnabled    = j.value("snapEnabled",    state.snapEnabled);
    state.snapTranslate  = j.value("snapTranslate",  state.snapTranslate);
    state.snapRotate     = j.value("snapRotate",     state.snapRotate);
    state.snapScale      = j.value("snapScale",      state.snapScale);

    // Keybinds (per field; missing keys leave defaults)
    if (j.contains("keybinds")) {
        const auto& kb = j["keybinds"];
        auto bind = [&](const char* name, KeyBind& out) {
            if (kb.contains(name)) keybindFromJson(kb[name], out);
        };
        bind("saveScene",        state.keybinds.saveScene);
        bind("saveSceneAs",      state.keybinds.saveSceneAs);
        bind("loadScene",        state.keybinds.loadScene);
        bind("toggleStats",      state.keybinds.toggleStats);
        bind("toggleHierarchy",  state.keybinds.toggleHierarchy);
        bind("toggleInspector",  state.keybinds.toggleInspector);
        bind("toggleBottom",     state.keybinds.toggleBottom);
        bind("toggleEditor",     state.keybinds.toggleEditor);
        bind("openPreferences",  state.keybinds.openPreferences);
        bind("deleteEntity",     state.keybinds.deleteEntity);
        bind("deselect",         state.keybinds.deselect);
        bind("duplicate",        state.keybinds.duplicate);
        bind("focusSelected",    state.keybinds.focusSelected);
        bind("gizmoSelect",      state.keybinds.gizmoSelect);
        bind("gizmoTranslate",   state.keybinds.gizmoTranslate);
        bind("gizmoRotate",      state.keybinds.gizmoRotate);
        bind("gizmoScale",       state.keybinds.gizmoScale);
        bind("gizmoToggleSpace", state.keybinds.gizmoToggleSpace);
    }

    // Recent scenes
    state.recentScenes.clear();
    if (j.contains("recentScenes") && j["recentScenes"].is_array()) {
        for (const auto& p : j["recentScenes"]) {
            if (p.is_string()) state.recentScenes.push_back(p.get<std::string>());
            if (state.recentScenes.size() >= EditorState::MaxRecentScenes) break;
        }
    }

    return true;
}

bool save(const EditorState& state) {
    json j;
    j["showStats"]         = state.showStats;
    j["showHierarchy"]     = state.showHierarchy;
    j["showInspector"]     = state.showInspector;
    j["showBottom"]        = state.showBottom;
    j["leftPanelWidth"]    = state.leftPanelWidth;
    j["rightPanelWidth"]   = state.rightPanelWidth;
    j["bottomPanelHeight"] = state.bottomPanelHeight;
    j["gizmoOperation"]    = static_cast<int>(state.gizmoOperation);
    j["gizmoMode"]         = static_cast<int>(state.gizmoMode);
    j["snapEnabled"]       = state.snapEnabled;
    j["snapTranslate"]     = state.snapTranslate;
    j["snapRotate"]        = state.snapRotate;
    j["snapScale"]         = state.snapScale;

    json kb;
    kb["saveScene"]        = keybindToJson(state.keybinds.saveScene);
    kb["saveSceneAs"]      = keybindToJson(state.keybinds.saveSceneAs);
    kb["loadScene"]        = keybindToJson(state.keybinds.loadScene);
    kb["toggleStats"]      = keybindToJson(state.keybinds.toggleStats);
    kb["toggleHierarchy"]  = keybindToJson(state.keybinds.toggleHierarchy);
    kb["toggleInspector"]  = keybindToJson(state.keybinds.toggleInspector);
    kb["toggleBottom"]     = keybindToJson(state.keybinds.toggleBottom);
    kb["toggleEditor"]     = keybindToJson(state.keybinds.toggleEditor);
    kb["openPreferences"]  = keybindToJson(state.keybinds.openPreferences);
    kb["deleteEntity"]     = keybindToJson(state.keybinds.deleteEntity);
    kb["deselect"]         = keybindToJson(state.keybinds.deselect);
    kb["duplicate"]        = keybindToJson(state.keybinds.duplicate);
    kb["focusSelected"]    = keybindToJson(state.keybinds.focusSelected);
    kb["gizmoSelect"]      = keybindToJson(state.keybinds.gizmoSelect);
    kb["gizmoTranslate"]   = keybindToJson(state.keybinds.gizmoTranslate);
    kb["gizmoRotate"]      = keybindToJson(state.keybinds.gizmoRotate);
    kb["gizmoScale"]       = keybindToJson(state.keybinds.gizmoScale);
    kb["gizmoToggleSpace"] = keybindToJson(state.keybinds.gizmoToggleSpace);
    j["keybinds"] = std::move(kb);

    j["recentScenes"] = state.recentScenes;

    std::ofstream out(path());
    if (!out.good()) return false;
    out << j.dump(2);
    return true;
}

}  // namespace EditorSettings
}  // namespace Engine
