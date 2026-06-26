#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/editor_settings.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "framework/editor_state.h"
#include "io/project_paths.h"

namespace Engine {
namespace EditorSettings {

namespace {
using nlohmann::json;

/**
 * @brief Bumped when a load-incompatible change is made to the schema.
 *
 * Loaders with a lower version fall back to defaults rather than
 * guessing at fields that no longer exist or have different meanings.
 */
constexpr int FILE_VERSION = 1;

json keybindToJson(const KeyBind& k) {
    return json{ {"key", static_cast<int>(k.key)}, {"mods", k.mods} };
}
void keybindFromJson(const json& j, KeyBind& k) {
    k.key  = static_cast<ImGuiKey>(j.value("key",  static_cast<int>(ImGuiKey_None)));
    k.mods = static_cast<uint8_t>(j.value("mods", 0));
}

/**
 * @brief One (json name, member) row per configurable keybind.
 *
 * Load and save both iterate this table, so the two directions can never
 * drift apart - adding a keybind to EditorKeybinds only needs one new row here.
 */
struct KeybindField {
    const char* name;
    KeyBind EditorKeybinds::* field;
};
constexpr KeybindField KEYBIND_FIELDS[] = {
    { "saveScene",        &EditorKeybinds::saveScene        },
    { "saveSceneAs",      &EditorKeybinds::saveSceneAs      },
    { "loadScene",        &EditorKeybinds::loadScene        },
    { "undo",             &EditorKeybinds::undo             },
    { "redo",             &EditorKeybinds::redo             },
    { "toggleHierarchy",  &EditorKeybinds::toggleHierarchy  },
    { "toggleInspector",  &EditorKeybinds::toggleInspector  },
    { "toggleBottom",     &EditorKeybinds::toggleBottom     },
    { "toggleEditor",     &EditorKeybinds::toggleEditor     },
    { "openPreferences",  &EditorKeybinds::openPreferences  },
    { "deleteEntity",     &EditorKeybinds::deleteEntity     },
    { "deselect",         &EditorKeybinds::deselect         },
    { "duplicate",        &EditorKeybinds::duplicate        },
    { "focusSelected",    &EditorKeybinds::focusSelected    },
    { "frameAll",         &EditorKeybinds::frameAll         },
    { "gizmoSelect",      &EditorKeybinds::gizmoSelect      },
    { "gizmoTranslate",   &EditorKeybinds::gizmoTranslate   },
    { "gizmoRotate",      &EditorKeybinds::gizmoRotate      },
    { "gizmoScale",       &EditorKeybinds::gizmoScale       },
    { "gizmoToggleSpace", &EditorKeybinds::gizmoToggleSpace },
};
} // namespace

std::string path() {
    return (ProjectPaths::root() / "editor_settings.json").string();
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
    if (v != FILE_VERSION) {
        // Future-incompatible: better to start from documented defaults than
        // to load fields with semantically different meanings.
        LOG_WARNING("EditorSettings::load: file version %d != expected %d. Using defaults.",
            v, FILE_VERSION);
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
        for (const KeybindField& f : KEYBIND_FIELDS) {
            if (kb.contains(f.name)) keybindFromJson(kb[f.name], state.keybinds.*f.field);
        }
    }

    // Recent scenes
    state.recentScenes.clear();
    if (j.contains("recentScenes") && j["recentScenes"].is_array()) {
        for (const auto& p : j["recentScenes"]) {
            if (p.is_string()) state.recentScenes.push_back(p.get<std::string>());
            if (state.recentScenes.size() >= EditorState::MAX_RECENT_SCENES) break;
        }
    }

    return true;
}

bool save(const EditorState& state) {
    json j;
    j["version"]           = FILE_VERSION;
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
    for (const KeybindField& f : KEYBIND_FIELDS) {
        kb[f.name] = keybindToJson(state.keybinds.*f.field);
    }
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
