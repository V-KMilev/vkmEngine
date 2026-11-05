#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief How far the backend is through what it owes before a world shows: the programs it builds
 *        at startup, then a world drawn behind the loading screen and the bakes its first frames
 *        start - its light, its reflections, its lightmaps - in steps of each; and whether the
 *        splash it was last shown can be drawn.
 *
 * A world with nothing to bake is ready once nothing holds its bakes back (RenderView::worldArriving).
 * The steps are not of one length: a share of them is a share of the work only roughly.
 */
struct RenderReadiness {
    uint32_t done          = 0;      ///< Steps done.
    uint32_t total         = 0;      ///< Steps started, or one owed while an arriving world holds them.
    uint32_t compiled      = 0;      ///< Startup programs compiled and checked; no world is drawn before
                                     ///< all are.
    uint32_t programs      = 0;      ///< Startup programs still owed or compiled; 0 once all are.
    bool     splashPending = false;  ///< The splash's images are still being made off the main thread:
                                     ///< SplashSystem holds its clock until they are drawn.
    bool     skyPending    = false;  ///< The sky's image is still being decoded off the main thread: an
                                     ///< arriving world's first bakes wait for it (RenderSystem's
                                     ///< ArrivalHold).

    bool compiling() const { return compiled < programs; }  ///< No world can be drawn yet.
    bool ready() const     { return done >= total && !compiling(); }
};

} // namespace Vkm::Engine
