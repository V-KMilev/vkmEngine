#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "base/reflect/reflect.h"
#include "io/project/tick_rate.h"
#include "platform/input/input_binding.h"
#include "io/project/loading_screen.h"
#include "io/project/render_settings.h"

namespace Vkm::Engine {

namespace Config {

    // Seat cap: a host's per-tick work scales with it, and it comes from a
    // hand-authored project.json.
    constexpr uint32_t MAX_PLAYERS_PER_GAME = 64;

    /**
     * @brief What a project's requested seat count is honored as.
     *
     * Zero is kept: the session then refuses to host.
     *
     * @param seats The count as asked for.
     * @return It, clamped to [0, MAX_PLAYERS_PER_GAME].
     */
    constexpr uint32_t clampSeats(uint32_t seats) {
        return seats > MAX_PLAYERS_PER_GAME ? MAX_PLAYERS_PER_GAME : seats;
    }

} // namespace Config

/**
 * @brief One logo a project shows at startup, and how long for.
 *
 * The fades come on top of seconds, so an entry takes longer than it names.
 */
struct SplashEntry {
    std::string image;           ///< Image to show, relative to the project root.
    float       seconds = 1.5f;  ///< Time held at full opacity.

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
    static constexpr size_t MAX_PHYSICS_LAYERS = 32;  ///< The most physics layers: one per bit of a
                                                      ///< Rigidbody's layer and collidesWith.

    std::string name;                                  ///< Display name; titles the window. See loadProject.
    std::string description;                           ///< One line on what it is, for the editor's start
                                                       ///< screen.
    std::string picture;                               ///< A 16:9 image over its start-screen card;
                                                       ///< root-relative.
    std::string version;                               ///< The game's own, which its packages are named by.
    std::string engineVersion;                         ///< Engine version this was authored against.
    std::string entryScene;                            ///< Scene to boot, relative to the project root.
    uint32_t    tickRate = Config::DEFAULT_TICK_RATE;  ///< Simulation ticks per second: the project's, not
                                                       ///< the engine's, as a networked game's wire is
                                                       ///< clocked by it.

    uint32_t    maxPlayers = 10;  ///< How many players this game is for. A property of the game, not of a
                                  ///< run. A connection past the last seat is refused with a reason. Zero is
                                  ///< kept and means it: the session refuses to host.

    uint16_t    netPort = 27750;  ///< The port this game is served on unless a run says otherwise. A port
                                  ///< given on a host's command line wins over this one.

    std::vector<SplashEntry> splash;  ///< Logos shown after the engine's own, in the order listed. Usually
                                      ///< empty.

    LoadingScreen loading;  ///< What the project adds to the loading screen: tips, its own screen, whether
                            ///< the water shows.

    std::vector<FontEntry> fonts;  ///< Fonts of the game's own, baked as the runtime starts and as the editor
                                   ///< opens it. Usually empty.

    std::vector<InputAction> actions;  ///< The game's input actions and their default bindings
                                       ///< (InputMap::setActions), in name order.

    std::vector<std::string> physicsLayers;  ///< What the author calls each physics layer: entry i names the
                                             ///< layer of bit i. Names only, for the editor to show; bodies
                                             ///< hold the bits, and an empty or missing entry is shown by its
                                             ///< number. At most MAX_PHYSICS_LAYERS.

    RenderSettings render;  ///< What the game looks like, as the author left it. Project, not editor, state:
                            ///< it passes Roots' "would you commit this?". The debug view and the grid
                            ///< are the host's (HostChrome::Display).
};

/**
 * @brief Read the project.json at @p projectRoot.
 *
 * A missing or malformed file is not fatal: @p out keeps its defaults and only
 * the reason is logged. Either way the name is project.json's "name", else the
 * name of the project's folder. A key or render setting of the wrong type keeps its value
 * and warns, as one in the loading block does (a tip that is no text keeps the tips as they were),
 * and the rest of the file is still read; an entry of the other lists that will not read is skipped.
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
 * does not parse is refused rather than replaced. A field at its default is no key, the render
 * block's fields too, so the file holds what the project chose.
 * actions, splash, fonts and loading are written only when they differ from what the file reads
 * as, so a list left alone keeps the entries loadProject skips. The render block is rebuilt from
 * RenderSettings' reflect block, every field it lists away from its default and nothing else.
 * physicsLayers is written without its trailing unnamed entries, and not at all when none is named.
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

/**
 * @brief Name @p scene as the one @p projectRoot's game starts in, when its project.json names
 *        none: its entryScene alone, every other key left as it was.
 *
 * @param projectRoot Directory holding project.json.
 * @param scene       The scene, relative to the project root.
 * @return True when the file was written; false, writing nothing, when it names one already, is
 *         missing or does not parse.
 */
[[nodiscard]] bool setEntrySceneIfNone(const std::filesystem::path& projectRoot, const std::string& scene);

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
 * The answer is absolute and normalized, so it compares as a string.
 *
 * @param start Directory or file to search from.
 * @return The project root, or empty when no project.json is found above @p start.
 */
std::filesystem::path findProjectRoot(const std::filesystem::path& start);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::SplashEntry)
    VKM_F(image)
    VKM_F(seconds)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::FontEntry)
    VKM_F(name)
    VKM_F(file)
VKM_REFLECT_END()

// What project.json says in one value each; the lists - logos, fonts, the loading screen, the input
// actions, the physics layers - are read on their own, as an entry that will not read is skipped.
VKM_REFLECT_BEGIN(::Vkm::Engine::Project)
    VKM_F(name, "Titles the window and names the project everywhere")
    VKM_F(description, "One line on what it is, shown on the start screen")
    VKM_F(picture, "A 16:9 image in the project, over its card on the start screen's Examples")
    VKM_F(version, "The game's own version, which names its packages")
    VKM_F(engineVersion, "The engine that last wrote this file")
    VKM_F(entryScene, "The scene the runtime and the server boot, relative to the project root")
    VKM_F(tickRate, Config::MIN_TICK_RATE, Config::MAX_TICK_RATE, "Simulation ticks a second, at both ends")
    VKM_F(maxPlayers, 0.0f, Config::MAX_PLAYERS_PER_GAME, "Seats this game has; one more is refused")
    VKM_F(netPort, 1024.0f, 65535.0f, "Where the game is served unless a run passes --port")
    VKM_F(render)
VKM_REFLECT_END()
