#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/editor_settings.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <type_traits>

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

/**
 * @brief Visit each persisted (json-key, member) scalar in one list.
 *
 * Both load and save walk this single function, so the two directions can
 * never list different fields - the only thing that differs is the visitor
 * @p f (read from json vs. write to json). @p State is EditorState for load
 * and const EditorState for save, which keeps save const-correct. Enums and
 * the keybinds / recentScenes blocks are handled outside this list.
 */
template <typename State, typename Fn>
void visitScalarFields(State& state, Fn&& f) {
    f("showHierarchy",     state.showHierarchy);
    f("showInspector",     state.showInspector);
    f("showBottom",        state.showBottom);
    f("leftPanelWidth",    state.leftPanelWidth);
    f("rightPanelWidth",   state.rightPanelWidth);
    f("bottomPanelHeight", state.bottomPanelHeight);
    f("gizmoOperation",    state.gizmoOperation);
    f("gizmoMode",         state.gizmoMode);
    f("snapEnabled",       state.snapEnabled);
    f("snapTranslate",     state.snapTranslate);
    f("snapRotate",        state.snapRotate);
    f("snapScale",         state.snapScale);
}
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

    // Panel visibility / sizes, gizmo defaults, snap - read each field from j,
    // leaving the in-struct default when the key is absent. Enums round-trip
    // through their underlying int.
    visitScalarFields(state, [&](const char* key, auto& member) {
        using M = std::decay_t<decltype(member)>;
        if constexpr (std::is_enum_v<M>)
            member = static_cast<M>(j.value(key, static_cast<int>(member)));
        else
            member = j.value(key, member);
    });

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
    j["version"] = FILE_VERSION;
    visitScalarFields(state, [&](const char* key, const auto& member) {
        using M = std::decay_t<decltype(member)>;
        if constexpr (std::is_enum_v<M>)
            j[key] = static_cast<int>(member);
        else
            j[key] = member;
    });

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
