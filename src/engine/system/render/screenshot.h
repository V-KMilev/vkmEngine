#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "system/render/render_backend.h"

namespace Vkm::Engine {

/**
 * @brief Write an 8-bit RGB image to @p path, in the form its extension names.
 *
 * A `.png` is the cheapest PNG the encoder makes, from a copy of it no other writer shares. A `.rgb`
 * is the bare rows and nothing more, so images written one after another into one pipe are raw
 * video, for a reader that knows their size.
 *
 * @param path   Where it goes, a `.png` or a `.rgb`; the directory must exist.
 * @param width  Image width in texels.
 * @param height Image height in texels.
 * @param rgb    width * height * 3 bytes, top row first, rows unpadded.
 * @return Whether the file was written; false for any other extension.
 */
bool writeScreenshotFile(
    const std::string& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgb
);

/**
 * @brief Writes screenshots on the ThreadPool, one at a time in the order given, so the frame that
 *        asked never waits on an encode and frames streamed into one pipe keep their order.
 */
class ScreenshotWriter {
    public:
        ScreenshotWriter() = default;
        ~ScreenshotWriter() = default;

        ScreenshotWriter(const ScreenshotWriter& other) = delete;
        ScreenshotWriter& operator=(const ScreenshotWriter& other) = delete;

        ScreenshotWriter(ScreenshotWriter && other) = delete;
        ScreenshotWriter& operator=(ScreenshotWriter && other) = delete;

    public:
        /**
         * @brief Write @p frame to @p path (writeScreenshotFile) on a worker, after every frame
         *        given before it, and log how it went.
         *
         * @param path  From the game's request.
         * @param frame The pixels.
         */
        void write(std::string path, FramePixels frame);

        /// Write here what is still queued: for once ThreadPool::shutdown() has dropped its tasks.
        void finish();

    private:
        struct Shot {
            std::string path;
            FramePixels frame;
        };

        /**
         * @brief The writes not yet made, shared with the task making them.
         */
        struct Queue {
            std::mutex       mutex;
            std::deque<Shot> shots;
            bool             draining = false;  ///< Whether a task is queued or running to make them.
        };

        /// Make @p queue's writes in order until none is left.
        static void drain(Queue& queue);

    private:
        std::shared_ptr<Queue> m_queue = std::make_shared<Queue>();
};

} // namespace Vkm::Engine
