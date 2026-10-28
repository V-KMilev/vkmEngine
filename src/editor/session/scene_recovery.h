#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Copies of a scene's unsaved changes, kept where a crash cannot take them.
 *
 * The editor writes one while the scene is dirty, removes it once the changes are saved
 * or given up, and offers it back when a scene opens with a copy newer than its file.
 * Plain files, so it is tested without a window.
 */
namespace SceneRecovery {

/// Seconds between copies of a scene with unsaved changes.
inline constexpr float INTERVAL = 60.0f;

/**
 * @brief Where @p scenePath's copy is kept.
 *
 * Named by a hash of the scene's absolute path, so two projects' scenes of one name
 * keep apart; a scene never saved is named by its project's root.
 *
 * @param dir         The folder copies live in.
 * @param projectRoot The open project.
 * @param scenePath   The scene's file; empty for one never saved.
 * @return The copy's path, which need not exist.
 */
std::filesystem::path pathFor(
    const std::filesystem::path& dir,
    const std::filesystem::path& projectRoot,
    const std::string& scenePath
);

/**
 * @brief The folder beside @p copy that keeps its terrains' unsaved bodies (keepTerrainCopies).
 *
 * @param copy pathFor's answer.
 * @return The folder, which need not exist.
 */
std::filesystem::path terrainsOf(const std::filesystem::path& copy);

/**
 * @brief Whether a copy exists that is newer than the scene's file.
 *
 * A scene never saved, or whose file is gone, has only its copy, so any copy is newer.
 *
 * @param copy      pathFor's answer.
 * @param scenePath The scene's file; empty for one never saved.
 * @return True when there is something to offer back.
 */
bool hasNewer(const std::filesystem::path& copy, const std::string& scenePath);

/**
 * @brief Remove a copy, if there is one, and its terrains with it.
 *
 * @param copy pathFor's answer.
 */
void discard(const std::filesystem::path& copy);

} // namespace SceneRecovery

} // namespace Vkm::Engine
