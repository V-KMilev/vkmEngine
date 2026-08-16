#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/editor_settings.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "framework/editor_state.h"
#include "system/render/render_settings.h"
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

/**
 * @brief The persisted RenderSettings fields, one (json-key, member) row each.
 *
 * Same single-list contract as visitScalarFields: load and save walk this one
 * function so the two directions can never drift. Machine-quality tuning
 * belongs here (not in the scene) by the Environment/RenderSettings split.
 */
template <typename Settings, typename Fn>
void visitRenderFields(Settings& r, Fn&& f) {
    f("renderMode",            r.renderMode);
    f("gtao",                  r.gtao);
    f("bloom",                 r.bloom);
    f("probes",                r.probes);
    f("occlusionCulling",      r.occlusionCulling);
    f("gtaoRadius",            r.gtaoRadius);
    f("gtaoIntensity",         r.gtaoIntensity);
    f("gtaoPower",             r.gtaoPower);
    f("gtaoBias",              r.gtaoBias);
    f("bloomStrength",         r.bloomStrength);
    f("bloomThreshold",        r.bloomThreshold);
    f("bloomKnee",             r.bloomKnee);
    f("bloomRadius",           r.bloomRadius);
    f("msaaSamples",           r.msaaSamples);
    f("shadowResolution",      r.shadowResolution);
    f("grid",                  r.grid);
}

/**
 * @brief Where the recent-projects list lives.
 *
 * The engine root, not the open project. A list of projects you have opened is
 * how you get from one to the next, so keeping it inside a project means each
 * one remembers only the ones opened while it was open - and opening a second
 * project hides the list that would take you back to the first.
 *
 * @return Absolute path to the recents file.
 */
std::string recentsPath() {
    return (ProjectPaths::engineRoot() / "editor_recents.json").string();
}

/**
 * @brief Read the engine-wide recent-projects list into @p state.
 *
 * @param state Editor state whose recentProjects list is replaced.
 */
void loadRecentProjects(EditorState& state) {
    state.recentProjects.clear();

    std::ifstream in(recentsPath());
    if (!in.good()) return;

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        LOG_WARNING("Recent projects unreadable (%s); starting empty", e.what());
        return;
    }

    if (!j.contains("recentProjects") || !j["recentProjects"].is_array()) return;
    for (const auto& p : j["recentProjects"]) {
        if (p.is_string()) state.recentProjects.push_back(p.get<std::string>());
    }
}

/**
 * @brief Write the engine-wide recent-projects list back out.
 *
 * @param state Editor state supplying the list to persist.
 */
void saveRecentProjects(const EditorState& state) {
    nlohmann::json j;
    j["recentProjects"] = state.recentProjects;

    std::ofstream out(recentsPath());
    if (out) out << j.dump(4) << "\n";
}
} // namespace

std::string path() {
    return (ProjectPaths::projectRoot() / "editor_settings.json").string();
}

bool load(EditorState& state, RenderSettings& render) {
    // Separate file, separate lifetime: which projects you have opened is not
    // one project's business, so reading it must not sit behind this project's
    // settings file existing and parsing. Gated, the first open of a fresh
    // project would start with an empty list and the shutdown save - which
    // writes the recents unconditionally - would clobber the real history.
    loadRecentProjects(state);

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

    // An absent key leaves the in-struct default; enums round-trip through int.
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
        for (const KeybindEntry& e : KEYBINDS) {
            if (kb.contains(e.jsonName)) keybindFromJson(kb[e.jsonName], state.keybinds.*e.field);
        }
    }

    // Render settings (machine-quality tuning; absent keys keep defaults)
    if (j.contains("renderSettings")) {
        const auto& rs = j["renderSettings"];
        visitRenderFields(render, [&](const char* key, auto& member) {
            using M = std::decay_t<decltype(member)>;
            if constexpr (std::is_enum_v<M>)
                member = static_cast<M>(rs.value(key, static_cast<int>(member)));
            else
                member = rs.value(key, member);
        });
    }

    // Recent scenes, which genuinely do belong to the open project.
    state.recentScenes.clear();
    if (j.contains("recentScenes") && j["recentScenes"].is_array()) {
        for (const auto& p : j["recentScenes"]) {
            if (p.is_string()) state.recentScenes.push_back(p.get<std::string>());
            if (state.recentScenes.size() >= EditorState::MAX_RECENT_ENTRIES) break;
        }
    }

    return true;
}

bool save(const EditorState& state, const RenderSettings& render) {
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
    for (const KeybindEntry& e : KEYBINDS) {
        kb[e.jsonName] = keybindToJson(state.keybinds.*e.field);
    }
    j["keybinds"] = std::move(kb);

    json rs;
    visitRenderFields(render, [&](const char* key, const auto& member) {
        using M = std::decay_t<decltype(member)>;
        if constexpr (std::is_enum_v<M>)
            rs[key] = static_cast<int>(member);
        else
            rs[key] = member;
    });
    j["renderSettings"] = std::move(rs);

    j["recentScenes"] = state.recentScenes;

    saveRecentProjects(state);

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
