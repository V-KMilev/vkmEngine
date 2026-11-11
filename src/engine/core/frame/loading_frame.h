#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

class Scene;

/**
 * @brief What the loading screen shows this frame, or nothing; LoadingSystem's product.
 */
struct LoadingFrame {
    float       opacity      = 0.0f;     ///< 0..1; 0 shows nothing.
    float       content      = 1.0f;     ///< 0..1: how far the screen's content has come in after a splash.
    float       progress     = 0.0f;     ///< 0..1: how far the bar is filled.
    std::string status;                  ///< One line saying what is being waited on.
    std::string tip;                     ///< One of the project's tips, or none.
    float       tipShown     = 1.0f;     ///< 0..1: how far the tip has faded in, as the tips change.
    Scene*      screen       = nullptr;  ///< The project's own screen: UI canvases drawn over the water,
                                         ///< under the bar; or none.
    uint64_t    screenAssets = 0;        ///< The asset graph (ResourceManager::epoch) the screen was built
                                         ///< against: drawn only while it is current, as one swapped in later
                                         ///< in the frame leaves its handles stale.
    bool        water        = true;     ///< Whether the engine's water is drawn behind; else plain black.

    bool covering = true;  ///< A frame shown since a LoadExpected was already the cover, so a load may hold
                           ///< the next; always while a host authors the world, which no load waits on.

    bool isShowing() const { return opacity > 0.0f; }
};

} // namespace Vkm::Engine
