#pragma once

#include <string>

namespace Vkm::Engine {

struct EditorState;
struct RenderSettings;

/**
 * @brief Persistent editor settings: the project's workspace, and the person's
 *        preferences.
 *
 * Two JSON documents. The project's (path()) holds the workspace it was left in:
 * shown panels, tool and gizmo space, the editor's view render fields, recent
 * scenes and each scene's last viewpoint. The person's, in ProjectPaths::userRoot()
 * (loadUser / saveUser), holds the recent projects and the Preferences, so they
 * survive switching projects and a read-only engine install. The dock layout is
 * ImGui's imgui.ini, beside the person's file. A missing or invalid file leaves
 * the defaults.
 */
namespace EditorSettings {

/**
 * @brief Load persisted settings into state.
 *
 * The recent scenes and scene viewpoints are emptied first regardless: they name
 * the project's files, so a project with no settings file must not show the last.
 *
 * @param state  Takes the saved fields; unchanged when the file is missing or
 *               invalid, the emptied lists aside.
 * @param render Its editor view fields (debug view, grid) take the file's
 *               renderSettings block; the game's look is project.json's.
 * @return false on a missing or invalid settings file, true on success.
 */
bool load(EditorState& state, RenderSettings& render);
/**
 * @brief Write the editor-owned settings to the settings file, and the per-user
 *        ones too (saveUser).
 *
 * Only while a project is open: without one path() is the engine's own directory.
 *
 * @param state  Its persistent fields are written.
 * @param render Its editor view fields go in the renderSettings block.
 * @return false on write failure, true on success.
 */
bool save(const EditorState& state, const RenderSettings& render);

/**
 * @brief Read the settings that follow the person rather than the project: the
 *        recent projects and the Preferences.
 *
 * Once, at startup, project or not: the start screen, drawn when no project
 * opened, offers the recent projects.
 *
 * @param state Its recentProjects list is replaced; its Preferences take the
 *              file's fields one by one.
 */
void loadUser(EditorState& state);

/**
 * @brief Write the per-user settings alone.
 *
 * What a session with no project open saves.
 *
 * @param state Supplies the values.
 * @return false when the file could not be written.
 */
bool saveUser(const EditorState& state);

/**
 * @brief Put the editor's defaults on the render fields that are its own view
 *        state rather than the game's look: the grid on.
 *
 * The engine defaults the grid off, since a game never draws it. Call wherever
 * those fields start over, and before load() so persisted values land over it.
 *
 * @param render Its view fields take the editor's defaults.
 */
void applyViewDefaults(RenderSettings& render);

/**
 * @brief Filesystem path the loader and saver operate on.
 *
 * Recomposed from the open project's root on each call, so it follows a switch.
 *
 * @return Absolute path to the settings JSON document.
 */
std::string path();

} // namespace EditorSettings

} // namespace Vkm::Engine
