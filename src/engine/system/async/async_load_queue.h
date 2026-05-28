#pragma once

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "resource/texture_asset.h"

namespace Engine {

/**
 * @brief One completed asynchronous texture decode.
 *
 * Workers push these onto the queue once stb_image returns. The main-
 * thread AsyncLoaderSystem drains the queue each frame and applies the
 * decoded pixel data to the live TextureAsset (created up-front in a
 * loading state by requestTextureAsync).
 */
struct TextureLoadCompletion {
    TextureHandle handle;
    std::vector<uint8_t> pixelData;
    uint32_t width    = 0;
    uint32_t height   = 0;
    int      channels = 0;
    bool     success  = false;   ///< False if stb_image_load failed; finaliser will warn and leave the asset empty.
};

/**
 * @brief Thread-safe drop-box for async-loaded assets awaiting main-thread
 *        finalisation.
 *
 * Workers (run on the ThreadPool) push completions; AsyncLoaderSystem on
 * the main thread drains them. The queue is a singleton because the same
 * worker code is invoked from many call sites and threading them all
 * through a context pointer would be noise.
 */
class AsyncLoadQueue {
    public:
        AsyncLoadQueue(const AsyncLoadQueue& other) = delete;
        AsyncLoadQueue& operator=(const AsyncLoadQueue& other) = delete;

        AsyncLoadQueue(AsyncLoadQueue && other) = delete;
        AsyncLoadQueue& operator=(AsyncLoadQueue && other) = delete;

    public:
        static AsyncLoadQueue& get();

        void pushTexture(TextureLoadCompletion completion);

        /// Move every pending completion out under one lock and return them.
        /// Empty if there's nothing pending. Called once per frame by
        /// AsyncLoaderSystem on the main thread.
        std::vector<TextureLoadCompletion> drainTextures();

    private:
        AsyncLoadQueue() = default;

        std::mutex                          m_mutex;
        std::vector<TextureLoadCompletion>  m_textures;
};

} // namespace Engine
