#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/engine_config.h"
#include "system/render/render_settings.h"

namespace Vkm::Engine {

/**
 * @brief One logo a project shows at startup, and how long for.
 *
 * The fades come on top of seconds, so an entry takes longer than it names.
 */
struct SplashEntry {
    std::string image;            ///< Image to show, relative to the project root.
    float       seconds = 1.5f;   ///< Time held at full opacity.
};

/**
 * @brief What a project.json says about the game it describes.
 *
 * A project is a directory with a project.json at its root. engineVersion is
 * compared against the running engine so a mismatch is reported up front.
 */
struct Project {
    std::string name         = "Untitled";  ///< Display name; titles the window.
    std::string description;                ///< One line on what it is, for the editor's start screen.
    std::string version;                    ///< The game's own, which its packages are named by.
    std::string engineVersion;              ///< Engine version this was authored against.
    std::string entryScene;                 ///< Scene to boot, relative to the project root.
    uint32_t    tickRate = Config::DEFAULT_TICK_RATE;  ///< Simulation ticks per second.

    /**
     * @brief How many players this game is for.
     *
     * A property of the game, not of a run. A connection past the last seat is
     * refused with a reason.
     */
    uint32_t    maxPlayers = 10;

    /**
     * @brief The port this game is served on unless a run says otherwise.
     *
     * A port given on a host's command line wins over this one.
     */
    uint16_t    netPort = 27750;

    /// Logos shown after the engine's own, in the order listed. Usually empty.
    std::vector<SplashEntry> splash;

    /**
     * @brief What the game looks like, as the author left it.
     *
     * Project, not editor, state: it passes ProjectPaths' "would you commit
     * this?". Debug view state is not in here; see visitShippedRenderFields.
     */
    RenderSettings render;
};

/**
 * @brief Read the project.json at @p projectRoot.
 *
 * A missing or malformed file is not fatal: @p out keeps its defaults and only
 * the reason is logged.
 *
 * @param projectRoot Directory expected to contain project.json.
 * @param out         Filled on success; untouched fields keep their defaults.
 * @return True when a project.json was read and parsed.
 */
bool loadProject(const std::filesystem::path& projectRoot, Project& out);

/**
 * @brief Write @p project back to the project.json at @p projectRoot.
 *
 * Read-modify-write: keys this struct does not describe are left as they were.
 * splash is not written, since loadProject drops entries naming no image and
 * writing the list back would delete them; an edit to it is not saved.
 *
 * @param projectRoot Directory holding project.json; it is created if missing.
 * @param project     What to record.
 * @return True when the file was written.
 */
[[nodiscard]] bool saveProject(const std::filesystem::path& projectRoot, const Project& project);

/**
 * @brief Whether a project made for @p engineVersion opens and builds on this engine.
 *
 * The same major.minor release does: a module's build refuses another
 * (vkm_check_engine_version, cmake/gameplay_module.cmake). An unrecorded version
 * is not refused there either.
 *
 * @param engineVersion A project's engineVersion.
 * @return True when it names this engine's major.minor, or nothing.
 */
bool compatibleEngine(const std::string& engineVersion);

/**
 * @brief Locate the project a path refers to.
 *
 * Accepts the directory or any file inside it, walking up to a project.json.
 * The answer is absolute and normalised, so it compares as a string.
 *
 * @param start Directory or file to search from.
 * @return The project root, or empty when no project.json is found above @p start.
 */
std::filesystem::path findProjectRoot(const std::filesystem::path& start);

} // namespace Vkm::Engine
