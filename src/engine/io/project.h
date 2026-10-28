#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/engine_config.h"
#include "system/loading/loading_screen.h"
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

    bool operator==(const SplashEntry& other) const {
        return image == other.image && seconds == other.seconds;
    }
    bool operator!=(const SplashEntry& other) const { return !(*this == other); }
};

/**
 * @brief A font a project draws its UI text in, beside the engine's own: a UIText names it.
 */
struct FontEntry {
    std::string name;  ///< The FontAsset's name, as UIText::font names it.
    std::string file;  ///< The .ttf, relative to the project root.

    bool operator==(const FontEntry& other) const { return name == other.name && file == other.file; }
    bool operator!=(const FontEntry& other) const { return !(*this == other); }
};

/**
 * @brief What a project.json says about the game it describes.
 *
 * A project is a directory with a project.json at its root. engineVersion is
 * compared against the running engine so a mismatch is reported up front.
 */
struct Project {
    /// The most physics layers: one per bit of a Rigidbody's layer and collidesWith.
    static constexpr size_t MAX_PHYSICS_LAYERS = 32;

    std::string name;                       ///< Display name; titles the window. See loadProject.
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

    /// What the project adds to the loading screen: tips, its own screen, whether the water shows.
    LoadingScreen loading;

    /// Fonts of the game's own, baked as the runtime starts and as the editor opens it. Usually empty.
    std::vector<FontEntry> fonts;

    /**
     * @brief What the author calls each physics layer: entry i names the layer of bit i.
     *
     * Names only, for the editor to show; bodies hold the bits. An empty or missing
     * entry is shown by its number. At most MAX_PHYSICS_LAYERS.
     */
    std::vector<std::string> physicsLayers;

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
 * the reason is logged. Either way the name is project.json's "name", else the
 * name of the project's folder.
 *
 * @param projectRoot Directory expected to contain project.json.
 * @param out         Filled on success; untouched fields keep their defaults.
 * @return True when a project.json was read and parsed.
 */
bool loadProject(const std::filesystem::path& projectRoot, Project& out);

/**
 * @brief Write @p project back to the project.json at @p projectRoot.
 *
 * Read-modify-write: keys this struct does not describe are left as they were, and a file that
 * does not parse is refused rather than replaced.
 * splash and fonts are written only when they differ from what the file reads as, so a list
 * left alone keeps the entries loadProject skips.
 *
 * @param projectRoot Directory holding project.json; it is created if missing.
 * @param project     What to record.
 * @return True when the file was written.
 */
[[nodiscard]] bool saveProject(const std::filesystem::path& projectRoot, const Project& project);

/**
 * @brief Write @p render alone into the project.json at @p projectRoot, as saveProject writes it;
 *        every other key is left as it was.
 *
 * @param projectRoot Directory holding project.json.
 * @param render      The project's render settings.
 * @return True when the file was written; false, writing nothing, when it does not parse.
 */
[[nodiscard]] bool saveProjectRender(const std::filesystem::path& projectRoot, const RenderSettings& render);

/**
 * @brief Name this engine as the one @p projectRoot's project is made for: its engineVersion
 *        alone, every other key of its project.json left as it was.
 *
 * @param projectRoot Directory holding project.json.
 * @return True when the file was written; false, writing nothing, when it does not parse.
 */
[[nodiscard]] bool saveProjectEngine(const std::filesystem::path& projectRoot);

/// Whether @p directory holds a project.json: is a project's root.
bool isProjectRoot(const std::filesystem::path& directory);

/// Whether @p a and @p b write the same render block into project.json (the shipped fields).
bool sameProjectRender(const RenderSettings& a, const RenderSettings& b);

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
