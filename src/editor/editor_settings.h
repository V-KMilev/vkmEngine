#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <tuple>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "base/reflect/reflect.h"
#include "io/json/json_file.h"
#include "io/json/json_number.h"

namespace Vkm::Engine {

struct EditorState;
struct HostChrome;

/**
 * @brief Persistent editor settings: the project's workspace, and the person's preferences.
 *
 * The project's file (path()) holds its Workspace, the display, the recent scenes and the viewpoints;
 * the person's, in Roots::userRoot() (loadUser), the recent projects and the Preferences. A
 * missing file, or one of another version, leaves the defaults.
 */
namespace EditorSettings {

/**
 * @brief Load the project's settings into @p state and @p chrome.
 *
 * The recent scenes, the viewpoints and the workspace take their defaults first, so a project with
 * no settings file does not show the last one's.
 *
 * @param state  Takes the saved fields; unchanged when the file is missing or invalid, the emptied
 *               lists aside.
 * @param chrome Takes the file's display block: the debug view and the grid.
 * @return false on a missing, invalid or other-version file.
 */
bool load(EditorState& state, HostChrome& chrome);

/**
 * @brief Write the project's settings, and the person's (saveUser).
 *
 * Only while a project is open: without one path() is the engine's own directory.
 *
 * @param state  Its persistent fields are written.
 * @param chrome Its display goes in the display block.
 * @return false when either file could not be written.
 */
bool save(const EditorState& state, const HostChrome& chrome);

/**
 * @brief Read the recent projects and the Preferences once at startup, project or not, for the start screen.
 *
 * @param state Its recentProjects list is replaced; its Preferences take the file's fields.
 */
void loadUser(EditorState& state);

/**
 * @brief Write the per-user settings alone, as a session with no project open does.
 *
 * @param state Supplies the values.
 * @return false when the file could not be written.
 */
bool saveUser(const EditorState& state);

/**
 * @brief Put the editor's display on the frame: the picture, and the ground's grid on.
 *
 * The engine defaults the grid off, as a game never draws it. Call before load(), which lands over it.
 */
void applyViewDefaults(HostChrome& chrome);

/**
 * @brief The open project's settings file, composed again on each call so it follows a switch.
 *
 * @return Absolute path to the settings JSON document.
 */
std::string path();

/**
 * @brief Where the project at @p projectRoot keeps these settings, open or not.
 *
 * @param projectRoot A project's directory.
 * @return The settings JSON document's path.
 */
std::filesystem::path pathIn(const std::filesystem::path& projectRoot);

/**
 * @brief Read a document of this build's schema version; another is refused whole, with a warning.
 *
 * Refused rather than read, since its fields' meanings may have changed.
 *
 * @param file    Path of the document.
 * @param label   What the log calls it.
 * @param version The schema version this build writes.
 * @param out     The document, when the result is true.
 * @return false when the file is missing, does not parse or is of another version.
 */
inline bool readVersioned(
    const std::filesystem::path& file,
    const char* label,
    int version,
    nlohmann::json& out
) {
    std::error_code missing;
    if (!std::filesystem::exists(file, missing)) return false;
    nlohmann::json read;
    if (!detail::readJsonFile(file, read, label)) return false;
    int found = 0;
    try {
        found = read.is_object() ? detail::numberAt<int>(read, "version", 0) : 0;
    } catch (const nlohmann::json::exception&) {
        found = 0;
    }
    if (found != version) {
        LOG_WARNING_C(
            "EDITOR",
            "%s is version %d, this editor reads %d; using defaults",
            label,
            found,
            version
        );
        return false;
    }
    out = std::move(read);
    return true;
}

/**
 * @brief Every number of the reflected @p value held to its field's VKM_F range, nested structs too.
 *
 * A settings file is hand-editable; nothing downstream was written for a value no control reaches.
 * A field whose min equals its max is unbounded.
 */
template <typename T>
void clampToRanges(T& value) {
    const auto clampField = [](auto& member, float lo, float hi) {
        using M = std::decay_t<decltype(member)>;
        if constexpr (Reflect::IS_REFLECTED<M>) {
            clampToRanges(member);
        } else if constexpr (std::is_arithmetic_v<M> && !std::is_same_v<M, bool>) {
            if (lo < hi) member = static_cast<M>(std::clamp(static_cast<float>(member), lo, hi));
        }
    };
    std::apply(
        [&](const auto&... field) { (clampField(value.*(field.ptr), field.min, field.max), ...); },
        Reflect::Traits<T>::fields()
    );
}

} // namespace EditorSettings

} // namespace Vkm::Engine
