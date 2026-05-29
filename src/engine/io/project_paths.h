#pragma once

#include <filesystem>

namespace Engine {

/**
 * @brief Canonical on-disk locations for the project, under the build-time
 *        APP_ROOT_DIR define.
 *
 * One place defines the project layout; callers compose specific files from
 * these directories (e.g. ProjectPaths::assets() / "_database.json") instead
 * of re-concatenating APP_ROOT_DIR at each site. Header-only inline, like the
 * other small free-function headers - every consumer already compiles with
 * APP_ROOT_DIR (the BuildInfo target), so no separate translation unit is
 * needed to hide the define.
 */
namespace ProjectPaths {

inline std::filesystem::path root()        { return std::filesystem::path(APP_ROOT_DIR); }
inline std::filesystem::path assets()      { return root() / "assets"; }
inline std::filesystem::path shaders()     { return root() / "shaders"; }
inline std::filesystem::path scenes()      { return root() / "scenes"; }
inline std::filesystem::path screenshots() { return root() / "screenshots"; }
inline std::filesystem::path envs()        { return assets() / "envs"; }

} // namespace ProjectPaths

} // namespace Engine
