#define VKM_LOG_CATEGORY "EDITOR"

#include "editor_settings.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <type_traits>
#include <string>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "editor_state.h"
#include "system/render/render_settings.h"
#include "io/json_file.h"
#include "io/project_paths.h"

namespace Vkm::Engine::EditorSettings {

namespace {
using nlohmann::json;

// Bumped when a change makes an older file mean something else; a file of
// another version is refused whole (readVersioned).
constexpr int FILE_VERSION      = 4;  ///< editor_settings.json, the project's.
constexpr int USER_FILE_VERSION = 1;  ///< editor_user.json, the person's.

/**
 * @brief Write a bind as ImGui's name for the key, not as its enum value.
 *
 * ImGui may renumber `ImGuiKey` between releases; a stored ordinal would then
 * mean a different key.
 *
 * @param k The bind to write.
 * @return The key's name and the modifier mask.
 */
json keybindToJson(const KeyBind& k) {
    return json{ {"key", ImGui::GetKeyName(k.key)}, {"mods", k.mods} };
}

/**
 * @brief Read a bind back, resolving the key by name.
 *
 * A key name this build does not know keeps the default rather than unbinding.
 *
 * @param j The object written by keybindToJson.
 * @param k The bind to fill: its key only where the file names one, its modifiers always.
 */
void keybindFromJson(const json& j, KeyBind& k) {
    const std::string name = j.value("key", std::string{});
    for (int candidate = ImGuiKey_NamedKey_BEGIN; candidate < ImGuiKey_NamedKey_END; ++candidate) {
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(candidate))) {
            k.key = static_cast<ImGuiKey>(candidate);
            break;
        }
    }
    k.mods = static_cast<uint8_t>(j.value("mods", 0));
}

/**
 * @brief Visit each persisted (json-key, member) scalar in one list.
 *
 * Load and save both walk it, so they cannot list different fields.
 * recentScenes and sceneViews are handled outside it.
 *
 * @tparam State EditorState for load, const EditorState for save.
 * @tparam Fn    Called as f(key, member) per field.
 * @param state Whose fields are visited.
 * @param f     The read or the write.
 */
template <typename State, typename Fn>
void visitScalarFields(State& state, Fn&& f) {
    f("showHierarchy",     state.showHierarchy);
    f("showInspector",     state.showInspector);
    f("showAssets",        state.showAssets);
    f("tool",              state.tool);
    f("gizmoMode",         state.gizmoMode);
}

/**
 * @brief The Preferences scalars, under the same single-list contract.
 *
 * The keybinds are a block of their own.
 *
 * @tparam Prefs Preferences for load, const Preferences for save.
 * @tparam Fn    Called as f(key, member) per field.
 * @param p Whose fields are visited.
 * @param f The read or the write.
 */
template <typename Prefs, typename Fn>
void visitPreferenceFields(Prefs& p, Fn&& f) {
    f("uiScale",               p.uiScale);
    f("vsync",                 p.vsync);
    f("fpsCap",                p.fpsCap);
    f("windowMode",            p.windowMode);
    f("snapEnabled",           p.snapEnabled);
    f("snapTranslate",         p.snapTranslate);
    f("snapRotate",            p.snapRotate);
    f("snapScale",             p.snapScale);
    f("cameraMoveSpeed",       p.camera.moveSpeed);
    f("cameraSpeedBoost",      p.camera.speedBoost);
    f("cameraLookSensitivity", p.camera.lookSensitivity);
    f("cameraZoomSensitivity", p.camera.zoomSensitivity);
    f("cameraMinPitch",        p.camera.minPitch);
    f("cameraMaxPitch",        p.camera.maxPitch);
}

/**
 * @brief The render fields that are this editor's view state, not the game's.
 *
 * `renderMode` (a debug buffer) and `grid` (editor chrome) are no shipped look;
 * the rest lives in `project.json` - see `visitShippedRenderFields` in
 * system/render/render_settings.h. Same single-list contract as visitScalarFields.
 *
 * @tparam Settings RenderSettings for load, const RenderSettings for save.
 * @tparam Fn       Called as f(key, member) per field.
 * @param r Whose view fields are visited.
 * @param f The read or the write.
 */
template <typename Settings, typename Fn>
void visitRenderFields(Settings& r, Fn&& f) {
    f("renderMode", r.renderMode);
    f("grid",       r.grid);
}

/**
 * @brief Read one persisted field, the way every visited field is read.
 *
 * An absent key or a value of the wrong type leaves the default, rather than
 * costing the rest of the file.
 *
 * @tparam M The field's type; an enum is read as int.
 * @param j Object holding the field.
 * @param key Its key.
 * @param member Field to fill.
 */
template <typename M>
void readField(const json& j, const char* key, M& member) {
    try {
        if constexpr (std::is_enum_v<M>)
            member = static_cast<M>(j.value(key, static_cast<int>(member)));
        else
            member = j.value(key, member);
    } catch (const json::type_error&) {
        LOG_WARNING("Editor settings: '%s' has the wrong type; keeping its default", key);
    }
}

/**
 * @brief Write one persisted field, the inverse of readField.
 *
 * @tparam M The field's type; an enum is written as int.
 * @param j Object receiving the field.
 * @param key Its key.
 * @param member Field to write.
 */
template <typename M>
void writeField(json& j, const char* key, const M& member) {
    if constexpr (std::is_enum_v<M>)
        j[key] = static_cast<int>(member);
    else
        j[key] = member;
}

/**
 * @brief Write the editor's viewpoint on one scene.
 *
 * @param view Viewpoint to write.
 * @return The position and the two angles, in radians.
 */
json viewpointToJson(const EditorViewpoint& view) {
    return json{
        {"position", {view.position.x, view.position.y, view.position.z}},
        {"yaw", view.yaw},
        {"pitch", view.pitch}
    };
}

/**
 * @brief Read a viewpoint back.
 *
 * @param j The object written by viewpointToJson.
 * @param view Filled from it; false leaves it untouched.
 * @return false when the entry is not one this writer made: a missing or
 *         wrong-length position, or an angle that is not a number.
 */
bool viewpointFromJson(const json& j, EditorViewpoint& view) {
    if (!j.is_object()) return false;
    const auto position = j.find("position");
    const auto yaw      = j.find("yaw");
    const auto pitch    = j.find("pitch");
    if (position == j.end() || !position->is_array() || position->size() != 3) return false;
    if (yaw == j.end() || !yaw->is_number() || pitch == j.end() || !pitch->is_number()) return false;
    for (const json& axis : *position) {
        if (!axis.is_number()) return false;
    }
    view.position = glm::vec3(
        (*position)[0].get<float>(),
        (*position)[1].get<float>(),
        (*position)[2].get<float>()
    );
    view.yaw   = yaw->get<float>();
    view.pitch = pitch->get<float>();
    return true;
}

/**
 * @brief Where the settings that follow the person rather than the project live.
 *
 * Not the project: there, the recent projects would be only those opened while
 * it was open. Not the engine: an SDK installed to /usr/local or Program Files
 * is read-only.
 *
 * @return Absolute path to the per-user editor settings file.
 */
std::string userPath() {
    return (ProjectPaths::userRoot() / "editor_user.json").string();
}

/**
 * @brief Read a settings document this build's schema wrote.
 *
 * Another version is refused whole, with a warning, rather than loading fields
 * whose meaning has changed.
 *
 * @param file    Path of the document.
 * @param label   What the log calls it.
 * @param version The schema version this build writes.
 * @param out     The document, when the result is true.
 * @return false when the file is missing, unreadable or of another version.
 */
bool readVersioned(const std::string& file, const char* label, int version, json& out) {
    std::ifstream in(file);
    if (!in.good()) return false;
    try {
        in >> out;
    } catch (const std::exception& e) {
        LOG_WARNING("%s unreadable (%s); using defaults", label, e.what());
        return false;
    }
    const int found = out.is_object() ? out.value("version", 0) : 0;
    if (found != version) {
        LOG_WARNING("%s is version %d, this editor reads %d; using defaults", label, found, version);
        return false;
    }
    return true;
}

} // namespace

void loadUser(EditorState& state) {
    state.recentProjects.clear();

    json j;
    if (!readVersioned(userPath(), "Per-user editor settings", USER_FILE_VERSION, j)) return;

    if (j.contains("preferences") && j["preferences"].is_object()) {
        const json& prefs = j["preferences"];
        visitPreferenceFields(state.prefs, [&](const char* key, auto& member) {
            readField(prefs, key, member);
        });
        if (prefs.contains("keybinds") && prefs["keybinds"].is_object()) {
            const json& kb = prefs["keybinds"];
            for (const KeybindEntry& e : KEYBINDS) {
                if (kb.contains(e.jsonName)) keybindFromJson(kb[e.jsonName], state.prefs.keybinds.*e.field);
            }
        }
        state.prefs = Preferences::bounded(state.prefs);
    }

    if (!j.contains("recentProjects") || !j["recentProjects"].is_array()) return;
    for (const auto& p : j["recentProjects"]) {
        if (p.is_string()) state.recentProjects.push_back(p.get<std::string>());
    }
}

bool saveUser(const EditorState& state) {
    json prefs;
    visitPreferenceFields(state.prefs, [&](const char* key, const auto& member) {
        writeField(prefs, key, member);
    });
    json kb;
    for (const KeybindEntry& e : KEYBINDS) {
        kb[e.jsonName] = keybindToJson(state.prefs.keybinds.*e.field);
    }
    prefs["keybinds"] = std::move(kb);

    json j;
    j["version"]        = USER_FILE_VERSION;
    j["recentProjects"] = state.recentProjects;
    j["preferences"]    = std::move(prefs);
    return detail::writeJsonFile(userPath(), j, "Per-user editor settings");
}

std::string path() {
    return (ProjectPaths::projectRoot() / "editor_settings.json").string();
}

bool load(EditorState& state, RenderSettings& render) {
    // Before any early return: the lists belong to the project being left.
    state.recentScenes.clear();
    state.sceneViews.clear();

    json j;
    if (!readVersioned(path(), "Editor settings", FILE_VERSION, j)) return false;

    visitScalarFields(state, [&](const char* key, auto& member) { readField(j, key, member); });

    // The editor's own view state; the game's look lives in project.json.
    if (j.contains("renderSettings")) {
        const auto& rs = j["renderSettings"];
        visitRenderFields(render, [&](const char* key, auto& member) { readField(rs, key, member); });
    }

    // Project-relative, so the list moves with it; a gone file's entry is dropped,
    // or Open Recent accumulates dead links.
    if (j.contains("recentScenes") && j["recentScenes"].is_array()) {
        for (const auto& p : j["recentScenes"]) {
            if (!p.is_string()) continue;
            std::string scenePath = ProjectPaths::resolveProjectPath(p.get<std::string>()).string();
            std::error_code existsEc;
            if (!std::filesystem::exists(scenePath, existsEc)) continue;
            state.recentScenes.push_back(std::move(scenePath));
            if (state.recentScenes.size() >= EditorState::MAX_RECENT_ENTRIES) break;
        }
    }

    // Kept only for scenes still on disk, by the rule the recents follow.
    if (j.contains("sceneViews") && j["sceneViews"].is_object()) {
        for (auto entry = j["sceneViews"].begin(); entry != j["sceneViews"].end(); ++entry) {
            std::error_code existsEc;
            if (!std::filesystem::exists(ProjectPaths::resolveProjectPath(entry.key()), existsEc)) continue;
            EditorViewpoint view;
            if (viewpointFromJson(entry.value(), view)) state.sceneViews[entry.key()] = view;
        }
    }

    return true;
}

void applyViewDefaults(RenderSettings& render) {
    render.grid = true;
}

bool save(const EditorState& state, const RenderSettings& render) {
    json j;
    j["version"] = FILE_VERSION;
    visitScalarFields(state, [&](const char* key, const auto& member) { writeField(j, key, member); });

    json rs;
    visitRenderFields(render, [&](const char* key, const auto& member) { writeField(rs, key, member); });
    j["renderSettings"] = std::move(rs);

    json recents = json::array();
    for (const std::string& scenePath : state.recentScenes) {
        recents.push_back(ProjectPaths::toProjectRelative(scenePath));
    }
    j["recentScenes"] = std::move(recents);

    json views = json::object();
    for (const auto& [scenePath, view] : state.sceneViews) views[scenePath] = viewpointToJson(view);
    j["sceneViews"] = std::move(views);

    // Both reported: losing the per-user file loses the way back to this project.
    const bool user    = saveUser(state);
    const bool project = detail::writeJsonFile(path(), j, "Editor settings");
    return user && project;
}

} // namespace Vkm::Engine::EditorSettings
