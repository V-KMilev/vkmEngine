#define VKM_LOG_CATEGORY "EDITOR"

#include "editor_settings.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "core/host_chrome.h"
#include "base/reflect/reflect.h"
#include "editor_state.h"
#include "io/json/json_file.h"
#include "io/json/json_number.h"
#include "io/project/roots.h"
#include "io/json/reflect_json.h"

// What the editor has the frame display: the debug view and the grid, which no game ships.
VKM_REFLECT_BEGIN(::Vkm::Engine::HostChrome::Display)
    VKM_F(mode)
    VKM_F(gridAxisX)
    VKM_F(gridAxisY)
    VKM_F(gridAxisZ)
VKM_REFLECT_END()

namespace Vkm::Engine::EditorSettings {

namespace {
using nlohmann::json;

// Bump when an older file would mean something else; a file of another version is refused whole.
constexpr int FILE_VERSION      = 7;  ///< editor_settings.json, the project's.
constexpr int USER_FILE_VERSION = 3;  ///< editor_user.json, the person's.

/**
 * @brief Write a bind as ImGui's name for the key, not as its enum value.
 *
 * ImGui may renumber `ImGuiKey` between releases, and a stored ordinal would then name another key.
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
    k.mods = detail::numberAt<uint8_t>(j, "mods", 0);
}

/**
 * @brief Where the settings that follow the person rather than the project live.
 *
 * Not the project, which would hold only its own recent projects; not the engine, which an SDK may
 * install read-only.
 *
 * @return Absolute path to the per-user editor settings file.
 */
std::string userPath() {
    return (Roots::userRoot() / "editor_user.json").string();
}

/// The object @p key holds in @p j, or an empty one: what the readers below take as "nothing".
const json& objectAt(const json& j, const char* key) {
    static const json s_none = json::object();
    const auto found = j.find(key);
    return found != j.end() && found->is_object() ? *found : s_none;
}

} // namespace

void loadUser(EditorState& state) {
    state.recentProjects.clear();

    json j;
    if (!readVersioned(userPath(), "Per-user editor settings", USER_FILE_VERSION, j)) return;

    const json& prefs = objectAt(j, "preferences");
    ReflectJson::fromJson(prefs, state.prefs);
    clampToRanges(state.prefs);
    const json& binds = objectAt(prefs, "keybinds");
    for (const KeybindEntry& e : KEYBINDS) {
        if (!binds.contains(e.jsonName)) continue;
        try {
            keybindFromJson(binds[e.jsonName], state.prefs.keybinds.*e.field);
        } catch (const nlohmann::json::exception&) {
            LOG_WARNING("Keybind '%s' is not a key and modifiers; keeping its default", e.jsonName);
        }
    }

    if (!j.contains("recentProjects") || !j["recentProjects"].is_array()) return;
    for (const auto& p : j["recentProjects"]) {
        if (p.is_string()) state.recentProjects.push_back(p.get<std::string>());
    }
}

bool saveUser(const EditorState& state) {
    json prefs = ReflectJson::toJson(state.prefs);
    json binds;
    for (const KeybindEntry& e : KEYBINDS) binds[e.jsonName] = keybindToJson(state.prefs.keybinds.*e.field);
    prefs["keybinds"] = std::move(binds);

    json j;
    j["version"]        = USER_FILE_VERSION;
    j["recentProjects"] = state.recentProjects;
    j["preferences"]    = std::move(prefs);
    return detail::writeJsonFile(userPath(), j, "Per-user editor settings");
}

std::string path() {
    return pathIn(Roots::projectRoot()).string();
}

std::filesystem::path pathIn(const std::filesystem::path& projectRoot) {
    return projectRoot / "editor_settings.json";
}

bool load(EditorState& state, HostChrome& chrome) {
    // Before any early return: these were the last project's, and an unwritten field takes its default.
    state.recentScenes.clear();
    state.sceneViews.clear();
    state.workspace = Workspace{};

    json j;
    if (!readVersioned(path(), "Editor settings", FILE_VERSION, j)) return false;

    ReflectJson::fromJson(objectAt(j, "workspace"), state.workspace);

    HostChrome::Display shown = chrome.display;
    ReflectJson::fromJson(objectAt(j, "display"), shown);
    chrome.display = shown;

    // Project-relative, so the list moves with it; a gone file's entry is dropped, or Open Recent
    // gathers dead links.
    if (j.contains("recentScenes") && j["recentScenes"].is_array()) {
        for (const auto& p : j["recentScenes"]) {
            if (!p.is_string()) continue;
            std::string scenePath = Roots::resolveProjectPath(p.get<std::string>()).string();
            std::error_code existsEc;
            if (!std::filesystem::exists(scenePath, existsEc)) continue;
            state.recentScenes.push_back(std::move(scenePath));
            if (state.recentScenes.size() >= EditorState::MAX_RECENT_ENTRIES) break;
        }
    }

    // Kept only for scenes still on disk, by the rule the recents follow.
    for (const auto& [scene, view] : objectAt(j, "sceneViews").items()) {
        std::error_code existsEc;
        if (!std::filesystem::exists(Roots::resolveProjectPath(scene), existsEc)) continue;
        EditorViewpoint read;
        ReflectJson::fromJson(view, read);
        clampToRanges(read);
        state.sceneViews[scene] = read;
    }
    return true;
}

void applyViewDefaults(HostChrome& chrome) {
    HostChrome::Display shown;
    shown.gridAxisX = true;  // X and Z: the ground
    shown.gridAxisZ = true;
    chrome.display  = shown;
}

bool save(const EditorState& state, const HostChrome& chrome) {
    json j;
    j["version"]   = FILE_VERSION;
    j["workspace"] = ReflectJson::toJson(state.workspace);
    j["display"]   = ReflectJson::toJson(chrome.display);

    json recents = json::array();
    for (const std::string& scenePath : state.recentScenes) {
        recents.push_back(Roots::toProjectRelative(scenePath));
    }
    j["recentScenes"] = std::move(recents);

    json views = json::object();
    for (const auto& [scenePath, view] : state.sceneViews) views[scenePath] = ReflectJson::toJson(view);
    j["sceneViews"] = std::move(views);

    // Both reported: losing the per-user file loses the way back to this project.
    const bool user    = saveUser(state);
    const bool project = detail::writeJsonFile(path(), j, "Editor settings");
    return user && project;
}

} // namespace Vkm::Engine::EditorSettings
