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
 * seconds is the time held at full opacity; the fade in and out are on top of
 * it, so an entry occupies more wall time than it names.
 */
struct SplashEntry {
    std::string image;            ///< Image to show, relative to the project root.
    float       seconds = 1.5f;   ///< Time held at full opacity.
};

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
    uint32_t    tickRate = Config::DEFAULT_TICK_RATE;  ///< Simulation ticks per second.

    /**
     * @brief How many players this game is for.
     *
     * A property of the game rather than of a run: a scene with four characters
     * authored into it is a four-player game wherever it is served. A
     * connection past the last seat is refused with a reason.
     *
     * Zero is not a special value - a game nobody can join is what a project
     * that never mentions networking already is, by having no vkmSetupNetwork
     * entry to say what a player is.
     */
    uint32_t    maxPlayers = 10;

    /**
     * @brief The port this game is served on unless a run says otherwise.
     *
     * Here so that serving a project and joining it need no argument to agree.
     * Where a particular run listens is still deployment data, so vkm_server
     * takes --port and it wins.
     */
    uint16_t    netPort = 27750;

    /// Logos shown after the engine's own, in the order listed. Usually empty.
    std::vector<SplashEntry> splash;

    /**
     * @brief What the game looks like, as the author left it.
     *
     * Here rather than in the editor's settings because it is the one render
     * state that answers yes to ProjectPaths' "would you commit this?": an
     * author who turns bloom off has decided something about the game, not
     * about their machine. It lived in `editor_settings.json` before, which is
     * both ignored by git and unread by every host except the editor - so a
     * shipped game rendered with the in-class defaults no matter what anyone
     * tuned.
     *
     * Debug view state is not in here; see visitShippedRenderFields.
     */
    RenderSettings render;
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
 * @brief Write @p project back to the project.json at @p projectRoot.
 *
 * Read-modify-write: the file is parsed, the fields this struct describes are
 * overwritten, and everything else in the document is left exactly as it was.
 * A project.json is hand-authored as often as it is written by a tool, and a
 * writer that rebuilt the document would quietly drop whatever it did not
 * happen to know about.
 *
 * splash is the one field read but not written. loadProject skips an entry
 * naming no image, so writing the list back would delete it from the file - and
 * nothing edits splash, so there is nothing to write.
 *
 * @param projectRoot Directory holding project.json; it is created if missing.
 * @param project     What to record.
 * @return True when the file was written.
 */
bool saveProject(const std::filesystem::path& projectRoot, const Project& project);

/**
 * @brief Locate the project a path refers to.
 *
 * Accepts either the project directory itself or any file inside it, and walks
 * up looking for project.json - so passing a scene finds the project owning it.
 *
 * The answer is absolute and normalised, so one project is named one way however
 * it was reached: callers compose on it and compare it as a string.
 *
 * @param start Directory or file to search from.
 * @return The project root, or empty when no project.json is found above @p start.
 */
std::filesystem::path findProjectRoot(const std::filesystem::path& start);

} // namespace Vkm::Engine
