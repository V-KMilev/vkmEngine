#pragma once

// What a frame published, read from a Game::run hook: the UI's draw, the pose, the cull, the particles.

#include "core/frame_context.h"
#include "core/system.h"

#include "harness/world.h"

namespace Vkm::Test {

/**
 * @brief A system last in the frame but for run()'s own, holding the frame's context for the test to read.
 */
class Watch : public System {
    public:
        void update(FrameContext& ctx) override { frame = &ctx; }

        FrameContext* frame = nullptr;  ///< The engine's, from the first frame on; its products are this
                                        ///< frame's until the next begins.
};

/**
 * @brief Watch @p game's frames.
 *
 * @param game Before run().
 * @return What the hooks of run() read the frame through.
 */
inline Watch& watch(Game& game) { return game.engine.addSystem<Watch>(SystemStage::Editor); }

} // namespace Vkm::Test
