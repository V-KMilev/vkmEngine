#pragma once

#include <filesystem>

namespace Engine {

/**
 * @brief Canonical on-disk locations, split by who owns them.
 *
 * Two roots, because two different things live on disk and they belong to
 * different people:
 *
 * - **engineRoot()** is what ships with the engine and is read-only to a game:
 *   shaders, the default UI font, editor icons. One copy serves every project.
 * - **projectRoot()** is the game being made: its scenes, its art, its asset
 *   library and the cooked cache derived from it. This is the part a user owns,
 *   edits, and version-controls.
 *
 * Before this split there was one root and the distinction did not exist, which
 * is why the engine could only ever run the game sitting inside its own repo.
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
inline std::filesystem::path screenshots() { return projectRoot() / "screenshots"; }
inline std::filesystem::path envs()        { return assets() / "envs"; }

// Asset database. `library` holds the editable per-asset recipe files (source of
// truth, version-controlled); `cooked` holds the derived binary cache keyed by
// recipe hash (regenerable, not version-controlled).
inline std::filesystem::path library()         { return projectRoot() / "library"; }
inline std::filesystem::path cooked()          { return projectRoot() / "cooked"; }
inline std::filesystem::path libraryManifest() { return library() / "_manifest.json"; }

} // namespace ProjectPaths

} // namespace Engine
