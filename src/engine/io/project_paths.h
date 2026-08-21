#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Canonical on-disk locations, split by who owns them.
 *
 * Three roots, because three different things live on disk and they belong to
 * different people:
 *
 * - **engineRoot()** is what ships with the engine and is read-only to a game:
 *   shaders, the default UI font, editor icons. One copy serves every project.
 *   An SDK installed to /usr/local or Program Files is not writable, so nothing
 *   the engine produces may be addressed from here.
 * - **projectRoot()** is the game being made: its scenes, its art, its asset
 *   library and the cooked cache derived from it. This is the part a user owns,
 *   edits, and version-controls.
 * - **userRoot()** is how one person likes their tools: the recent-projects
 *   list, the editor's window layout. It follows the user across projects and
 *   across engine installs, and it is the only root guaranteed writable.
 *
 * The split between the last two is the question "would you commit this?" A
 * scene is project data. A window layout is not - it belongs to whoever is
 * sitting in front of the editor, and writing it into a project would hand the
 * next person a layout they never chose.
 *
 * Callers compose specific files from these directories (e.g.
 * ProjectPaths::library() / "_manifest.json") rather than re-deriving a root.
 */
namespace ProjectPaths {

/**
 * @brief Directory holding the engine's own read-only data.
 *
 * A packaged build ships it beside the executable (or one level up, when the
 * exe sits in bin/); a development build falls back to the repo root recorded
 * at configure time.
 *
 * @return Absolute path to the engine root.
 */
std::filesystem::path engineRoot();

/**
 * @brief Point the project root at @p path.
 *
 * Set before anything composes a project path. The override itself takes effect
 * immediately - it is checked ahead of the fallback - but a path already built
 * from the old root is a plain string by then and will not follow.
 *
 * @param path Directory containing the project's project.json.
 */
void setProjectRoot(const std::filesystem::path& path);

/**
 * @brief Directory holding the project currently open.
 *
 * The path set by setProjectRoot() when there is one. Otherwise the engine root:
 * a development checkout is its own project, and a packaged game keeps its data
 * beside the executable, so both want the same directory.
 *
 * @return Absolute path to the project root.
 */
std::filesystem::path projectRoot();

/**
 * @brief Directory this user's own settings live in.
 *
 * The platform's convention for per-user application data - $XDG_CONFIG_HOME
 * (or ~/.config) on Linux, %APPDATA% on Windows - under a vkmEngine folder.
 * Falls back to the engine root only when the platform names no home directory
 * at all, which is the behaviour that predates this root.
 *
 * The directory exists when this returns: it holds nothing but files the engine
 * writes, so creating it here rather than at each writer is what keeps a caller
 * from having to remember. Resolved once, like engineRoot().
 *
 * @return Absolute path to the user's vkmEngine settings directory.
 */
std::filesystem::path userRoot();

/**
 * @brief Directory a host writes its log to when the project cannot hold one.
 *
 * $XDG_STATE_HOME (or ~/.local/state) on Linux, %LOCALAPPDATA% on Windows: a
 * log is state rather than settings, and on Windows it should not roam. Not
 * created here - bootHost creates the per-project subdirectory it actually
 * writes into.
 *
 * @return Absolute path to the user's vkmEngine log directory.
 */
std::filesystem::path userLogs();

/**
 * @brief Turn a stored reference into a path that can be opened.
 *
 * A relative reference resolves against the project root, never against the
 * working directory: that is the engine root in the editor and the runtime, and
 * the cooker does not pin one at all. An absolute reference passes through, for
 * a source that lives outside the project.
 *
 * @param path Reference as stored in a scene, a recipe or an asset name.
 * @return An absolute path.
 */
std::filesystem::path resolveProjectPath(const std::string& path);

/**
 * @brief The form of a path an asset should be named and recorded by.
 *
 * Project-relative whenever the file is under the project root, so the identity
 * a scene, a material reference and the asset library's on-disk layout are keyed
 * on does not carry the authoring machine's directory tree. A file outside the
 * project keeps its absolute path - it has no relative form - and the result is
 * always generic-separated, so a reference authored on Windows resolves here.
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
 * A project brings its own code: the module is the game, so it lives with the
 * game rather than with the engine that loads it.
 *
 * @return Absolute path to the project's binary directory.
 */
inline std::filesystem::path projectBin() { return projectRoot() / "bin"; }

// Project-owned: the game's own content, written by the editor.
inline std::filesystem::path assets()      { return projectRoot() / "assets"; }
inline std::filesystem::path scenes()      { return projectRoot() / "scenes"; }
inline std::filesystem::path prefabs()     { return projectRoot() / "prefabs"; }
inline std::filesystem::path screenshots() { return projectRoot() / "screenshots"; }
inline std::filesystem::path envs()        { return assets() / "envs"; }

// Asset database. `library` holds the editable per-asset recipe files (source of
// truth, version-controlled); `cooked` holds the derived binary cache keyed by
// recipe hash (regenerable, not version-controlled).
inline std::filesystem::path library()         { return projectRoot() / "library"; }
inline std::filesystem::path cooked()          { return projectRoot() / "cooked"; }
inline std::filesystem::path libraryManifest() { return library() / "_manifest.json"; }

} // namespace ProjectPaths

} // namespace Vkm::Engine
