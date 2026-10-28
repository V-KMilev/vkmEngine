#pragma once

namespace Vkm::Engine {

/**
 * @brief A world is about to be loaded: the loading screen covers the next frame.
 *
 * A load holds its frame, so the screen should already be the cover when it does: enqueued every
 * frame a load waits, until LoadingFrame::covering. The world loaded keeps the cover until it is
 * ready; a frame none is enqueued in lets the cover go.
 */
struct LoadExpected {};

} // namespace Vkm::Engine
