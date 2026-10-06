#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief One finished decode, landed on the main thread into the asset it was requested for.
 *
 * The slot keeps its identity and source; the decode brings the rest.
 *
 * @tparam Asset MeshAsset or TextureAsset.
 */
template<typename Asset>
struct LoadCompletion {
    Handle<Asset> handle;
    /// Resource::uid requested for; a scene load can hand its handle to a stranger.
    uint64_t      assetUid = 0;
    /// What the worker read; left empty when the decode failed.
    Asset         decoded;
};

using MeshLoadCompletion    = LoadCompletion<MeshAsset>;
using TextureLoadCompletion = LoadCompletion<TextureAsset>;

/**
 * @brief Thread-safe drop-box for async-loaded assets awaiting main-thread finalisation.
 *
 * One per process, so a worker task carries no pointer back to its request.
 */
class AsyncLoadQueue {
    public:
        ~AsyncLoadQueue() = default;

        AsyncLoadQueue(const AsyncLoadQueue& other) = delete;
        AsyncLoadQueue& operator=(const AsyncLoadQueue& other) = delete;

        AsyncLoadQueue(AsyncLoadQueue && other) = delete;
        AsyncLoadQueue& operator=(AsyncLoadQueue && other) = delete;

    public:
        static AsyncLoadQueue& get();

        void pushTexture(TextureLoadCompletion completion);
        void pushMesh(MeshLoadCompletion completion);

        /**
         * @brief Move every pending completion out under one lock; main thread only.
         *
         * drainMeshes is the same for meshes.
         *
         * @return The completions pushed since the last drain, oldest first.
         */
        std::vector<TextureLoadCompletion> drainTextures();
        std::vector<MeshLoadCompletion>    drainMeshes();

    private:
        AsyncLoadQueue() = default;

    private:
        std::mutex                          m_mutex;
        std::vector<TextureLoadCompletion>  m_textures;
        std::vector<MeshLoadCompletion>     m_meshes;
};

} // namespace Vkm::Engine
