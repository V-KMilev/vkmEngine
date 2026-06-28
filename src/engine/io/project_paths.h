#pragma once

#include <filesystem>

namespace Engine {

/**
 * @brief Canonical on-disk locations for the project.
 *
 * One place defines the project layout; callers compose specific files from
 * these directories (e.g. ProjectPaths::library() / "_manifest.json") instead
 * of re-deriving the root at each site.
 *
 * root() is resolved once at first use: a packaged build ships its data beside
 * the executable (detected by a `shaders/` folder next to the exe) and roots
 * there, so the game is relocatable; otherwise it falls back to the build-time
 * APP_ROOT_DIR (the dev repo root). root() is therefore defined out-of-line in
 * project_paths.cpp - resolving the executable path is platform code - while the
 * composing helpers below stay header-inline.
 */
namespace ProjectPaths {

std::filesystem::path root();

inline std::filesystem::path assets()      { return root() / "assets"; }
inline std::filesystem::path shaders()     { return root() / "shaders"; }
inline std::filesystem::path scenes()      { return root() / "scenes"; }
inline std::filesystem::path screenshots() { return root() / "screenshots"; }
inline std::filesystem::path envs()        { return assets() / "envs"; }

// Asset database. `library` holds the editable per-asset recipe files (source of
// truth, version-controlled); `cooked` holds the derived binary cache keyed by
// recipe hash (regenerable, not version-controlled). The manifest maps an
// asset's name to its recipe + cooked files.
inline std::filesystem::path library()         { return root() / "library"; }
inline std::filesystem::path cooked()          { return root() / "cooked"; }
inline std::filesystem::path libraryManifest() { return library() / "_manifest.json"; }

} // namespace ProjectPaths

} // namespace Engine
