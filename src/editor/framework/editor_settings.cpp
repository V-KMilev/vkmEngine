#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/editor_settings.h"
#include "framework/editor_state.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/system.h"   // APP_ROOT_DIR (via compile_definitions, actually)

namespace Engine {
namespace EditorSettings {

namespace {
    using nlohmann::json;

    /// Bumped when a load-incompatible change is made to the schema.
    /// Loaders with a lower version fall back to defaults rather than
    /// guessing at fields that no longer exist or have different meanings.
    constexpr int kFileVersion = 1;

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
    try {
        in >> j;
    } catch (const std::exception& e) {
        LOG_WARNING("EditorSettings::load: failed to parse %s - %s. Using defaults.",
            path().c_str(), e.what());
        return false;
    }

    const int v = j.value("version", 0);
    if (v != kFileVersion) {
        // Future-incompatible: better to start from documented defaults than
        // to load fields with semantically different meanings.
        LOG_WARNING("EditorSettings::load: file version %d != expected %d. Using defaults.",
            v, kFileVersion);
        return false;
    }

    // Panel visibility
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
        bind("undo",             state.keybinds.undo);
        bind("redo",             state.keybinds.redo);
        bind("toggleHierarchy",  state.keybinds.toggleHierarchy);
        bind("toggleInspector",  state.keybinds.toggleInspector);
        bind("toggleBottom",     state.keybinds.toggleBottom);
        bind("toggleEditor",     state.keybinds.toggleEditor);
        bind("runtimeSettings",  state.keybinds.runtimeSettings);
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
    j["version"]           = kFileVersion;
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
    kb["undo"]             = keybindToJson(state.keybinds.undo);
    kb["redo"]             = keybindToJson(state.keybinds.redo);
    kb["toggleHierarchy"]  = keybindToJson(state.keybinds.toggleHierarchy);
    kb["toggleInspector"]  = keybindToJson(state.keybinds.toggleInspector);
    kb["toggleBottom"]     = keybindToJson(state.keybinds.toggleBottom);
    kb["toggleEditor"]     = keybindToJson(state.keybinds.toggleEditor);
    kb["runtimeSettings"]  = keybindToJson(state.keybinds.runtimeSettings);
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

    // Atomic write: serialize to a sibling temp file then rename over the
    // target. A crash mid-write leaves the previous settings intact instead
    // of producing a half-written file the next launch can't parse.
    const std::string target = path();
    const std::string tmp    = target + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out.good()) {
            LOG_ERROR("EditorSettings::save: cannot open %s for write", tmp.c_str());
            return false;
        }
        out << j.dump(2);
        if (!out.good()) {
            LOG_ERROR("EditorSettings::save: write to %s failed", tmp.c_str());
            return false;
        }
    } // out closes here - flush + fd release before the rename

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        LOG_ERROR("EditorSettings::save: rename %s -> %s failed: %s",
            tmp.c_str(), target.c_str(), ec.message().c_str());
        std::filesystem::remove(tmp, ec);  // best-effort cleanup
        return false;
    }
    return true;
}

}  // namespace EditorSettings
}  // namespace Engine
