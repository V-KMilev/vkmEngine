#pragma once

// The built fixture modules (game.cpp, refused.cpp), and a project to put one in.

#include <filesystem>
#include <string>
#include <system_error>

#include "io/project/roots.h"

#include "harness/check.h"
#include "harness/scratch.h"

namespace Vkm::Test {

/**
 * @brief A fixture module as built.
 *
 * @param name "game_v1" or "game_v2", the module; "game_stale", "game_unversioned",
 *             "game_unregistering" or "game_offline", the ones a host refuses or plays offline.
 * @return Its file.
 */
inline std::filesystem::path fixtureModule(const std::string& name) {
    return std::filesystem::path(VKM_TEST_MODULE_DIR) / (
        VKM_TEST_MODULE_PREFIX + name + VKM_TEST_MODULE_SUFFIX
    );
}

/**
 * @brief Put fixture @p name where a host looks for the open project's module, over any there.
 *
 * @return Where it went.
 */
inline std::filesystem::path installModule(const std::string& name) {
    const std::filesystem::path to = Vkm::Engine::Roots::gameplayModule();
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::copy_file(
        fixtureModule(name),
        to,
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
    REQUIRE(!ec);
    return to;
}

/**
 * @brief Write the open project's project.json: a game at 60 ticks a second that boots @p entryScene.
 *
 * @param entryScene Its entry scene, relative to the project; empty names none.
 * @param more       More keys, written into the object as they are.
 */
inline void writeProjectFile(const std::string& entryScene, const std::string& more = "") {
    std::string text = "{\n  \"name\": \"fixture\",\n  \"tickRate\": 60,\n  \"maxPlayers\": 4";
    if (!entryScene.empty()) text += ",\n  \"entryScene\": \"" + entryScene + "\"";
    if (!more.empty()) text += ",\n  " + more;
    writeText(Vkm::Engine::Roots::projectRoot() / "project.json", text + "\n}\n");
}

} // namespace Vkm::Test
