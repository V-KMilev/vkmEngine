#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Canonical on-disk locations, split by who owns them.
 *
 * - **engineRoot()** ships with the engine and is read-only: an installed SDK is
 *   not writable, so nothing the engine produces is addressed from here.
 * - **projectRoot()** is the game: scenes, art, the asset library, the cooked cache.
 * - **userRoot()** is one person's tool settings, across projects and installs.
 *
 * Project or user is the question "would you commit this?"
 */
namespace ProjectPaths {

/**
 * @brief Directory holding the engine's own read-only data.
 *
 * Beside the executable or one level up when packaged; otherwise the repo root
 * recorded at configure time.
 *
 * @return Absolute path to the engine root.
 */
std::filesystem::path engineRoot();

/**
 * @brief Point the project root at @p path.
 *
 * Set before anything composes a project path: a path already built from the
 * old root will not follow.
 *
 * @param path Directory containing the project's project.json.
 */
void setProjectRoot(const std::filesystem::path& path);

/**
 * @brief Directory holding the project currently open.
 *
 * The path set by setProjectRoot(), else the engine root (a checkout is its own
 * project; a packaged game keeps its data beside the executable).
 *
 * @return Absolute path to the project root.
 */
std::filesystem::path projectRoot();

/**
 * @brief Directory this user's own settings live in.
 *
 * A vkmEngine folder under $XDG_CONFIG_HOME (or ~/.config) or %APPDATA%; the
 * engine root when there is no home or the folder cannot be created. Exists when
 * this returns; resolved once.
 *
 * @return Absolute path to the user's vkmEngine settings directory.
 */
std::filesystem::path userRoot();

/**
 * @brief Directory a host writes its log to when the project cannot hold one.
 *
 * Under $XDG_STATE_HOME (or ~/.local/state) or %LOCALAPPDATA%: a log is state,
 * not settings, and should not roam. Not created here; see bootHost.
 *
 * @return Absolute path to the user's vkmEngine log directory.
 */
std::filesystem::path userLogs();

/**
 * @brief Turn a stored reference into a path that can be opened.
 *
 * A relative reference resolves against the project root, never the working
 * directory (bootHost pins that to the engine root). An absolute one passes through.
 *
 * @param path Reference as stored in a scene, a recipe or an asset name.
 * @return An absolute path.
 */
std::filesystem::path resolveProjectPath(const std::string& path);

/**
 * @brief The form of a path an asset should be named and recorded by.
 *
 * Project-relative when under the project root, so identities do not carry the
 * authoring machine's tree; absolute otherwise. Always generic-separated, so a
 * reference authored on Windows resolves elsewhere.
 *
 * @param path Absolute or relative path to a source file or folder.
 * @return The reference to store.
 */
std::string toProjectRelative(const std::string& path);

// Engine-owned, read-only to a game.
inline std::filesystem::path engineShaders() { return engineRoot() / "shaders"; }
inline std::filesystem::path engineAssets()  { return engineRoot() / "assets"; }
inline std::filesystem::path engineFonts()   { return engineAssets() / "fonts"; }

/**
 * @brief Directory the project keeps its built gameplay module in.
 *
 * @return Absolute path to the project's binary directory.
 */
inline std::filesystem::path projectBin() { return projectRoot() / "bin"; }

// Project-owned: the game's own content.
inline std::filesystem::path assets()      { return projectRoot() / "assets"; }
inline std::filesystem::path scenes()      { return projectRoot() / "scenes"; }
inline std::filesystem::path prefabs()     { return projectRoot() / "prefabs"; }
inline std::filesystem::path screenshots() { return projectRoot() / "screenshots"; }
inline std::filesystem::path envs()        { return assets() / "envs"; }

// `library` holds the per-asset recipes (source of truth, version-controlled);
// `cooked` holds what the cook derives (AssetCook::cacheKey binaries plus manifest).
inline std::filesystem::path library()       { return projectRoot() / "library"; }
inline std::filesystem::path cooked()        { return projectRoot() / "cooked"; }
inline std::filesystem::path assetManifest() { return cooked() / "_manifest.json"; }

} // namespace ProjectPaths

} // namespace Vkm::Engine
