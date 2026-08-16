#pragma once

#include <filesystem>
#include <string>

namespace Engine {

/**
 * @brief What a project.json says about the game it describes.
 *
 * A project is a directory with a project.json at its root. That file is what
 * makes a directory a project rather than a folder of loose files, and what
 * lets the engine run a game that lives nowhere near the engine's own repo.
 * engineVersion is compared against the running engine so a project authored
 * against a different one says so up front rather than failing halfway through
 * a scene load.
 */
struct Project {
    std::string name         = "Untitled";  ///< Display name; titles the window.
    std::string engineVersion;              ///< Engine version this was authored against.
    std::string entryScene;                 ///< Scene to boot, relative to the project root.
};

/**
 * @brief Read the project.json at @p projectRoot.
 *
 * A missing or malformed file is not fatal: @p out keeps its defaults and the
 * caller runs an unnamed project with no entry scene, which is what a fresh
 * directory should do. Only the reason is logged.
 *
 * @param projectRoot Directory expected to contain project.json.
 * @param out         Filled on success; untouched fields keep their defaults.
 * @return True when a project.json was read and parsed.
 */
bool loadProject(const std::filesystem::path& projectRoot, Project& out);

/**
 * @brief Locate the project a path refers to.
 *
 * Accepts either the project directory itself or any file inside it, and walks
 * up looking for project.json - so passing a scene finds the project owning it.
 *
 * @param start Directory or file to search from.
 * @return The project root, or empty when no project.json is found above @p start.
 */
std::filesystem::path findProjectRoot(const std::filesystem::path& start);

} // namespace Engine
