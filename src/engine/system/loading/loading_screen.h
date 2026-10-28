#pragma once

#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief What a project adds to its loading screen, beside the engine's own bar and line; all of it
 *        optional.
 *
 * A project file's "loading" block (Project::loading), which LoadingSystem::setScreen takes.
 */
struct LoadingScreen {
    std::vector<std::string> tips;          ///< Lines shown one at a time under the bar, in turn.
    std::string              screen;        ///< A prefab of UI canvases drawn over the water; or none.
    bool                     water = true;  ///< Whether the engine's water is drawn under it.

    bool operator==(const LoadingScreen& other) const {
        return tips == other.tips && screen == other.screen && water == other.water;
    }
    bool operator!=(const LoadingScreen& other) const { return !(*this == other); }
};

} // namespace Vkm::Engine
