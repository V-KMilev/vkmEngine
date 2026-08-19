#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

/**
 * @brief One completed asynchronous texture decode.
 *
 * Workers push these once stb_image returns; the main-thread AsyncLoaderSystem
 * drains them each frame into the live TextureAsset (created up-front in a
 * loading state by requestTextureAsync).
 */
struct TextureLoadCompletion {
    TextureHandle handle;
    uint64_t      assetUid = 0;  ///< Resource::uid of the asset this decode was requested for; see AsyncLoaderSystem.
    std::vector<uint8_t> pixelData;
    uint32_t width    = 0;
    uint32_t height   = 0;
    int      channels = 0;
    bool     success  = false;   ///< False if the decode/read failed; finaliser will warn and leave the asset empty.

    /**
     * @brief Cooked textures already know their exact TextureParams (format, wrap,
     * filter), so they bypass the channel-count inference the stb path uses.
     * When set, the finaliser applies `params` verbatim instead of inferring.
     */
    bool          hasParams = false;
    TextureParams params{};
};

/**
 * @brief One completed asynchronous mesh decode (Assimp + vertex extraction).
 *
 * Same shape as the texture variant, drained into the live MeshAsset (created
 * in a loading state by requestModelMeshAsync). Bounds are already computed on
 * the worker, so the finaliser is a pure copy.
 */
struct MeshLoadCompletion {
    MeshHandle handle;
    uint64_t   assetUid = 0;     ///< Resource::uid of the asset this decode was requested for; see AsyncLoaderSystem.
    std::vector<Vertex>     vertices;
    std::vector<uint32_t>   indices;
    std::vector<SkinVertex> skin;
    std::string skeleton;        ///< Rig `skin` addresses; empty when the mesh is not skinned.
    glm::vec3 boundsMin{0};
    glm::vec3 boundsMax{0};
    float     skinRadius = 0.0f;
    bool      success = false;   ///< False if Assimp failed; finaliser warns and leaves the asset empty.
};

/**
 * @brief Thread-safe drop-box for async-loaded assets awaiting main-thread
 *        finalisation.
 *
 * Workers (run on the ThreadPool) push completions; AsyncLoaderSystem on the
 * main thread drains them. A singleton because the same worker code is invoked
 * from many call sites and threading a context pointer through them all would
 * be noise.
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
        void pushMesh   (MeshLoadCompletion    completion);

        /**
         * @brief Move every pending completion out under one lock and return them.
         * Empty if there's nothing pending. Called once per frame by
         * AsyncLoaderSystem on the main thread.
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
