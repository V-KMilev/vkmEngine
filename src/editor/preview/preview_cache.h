#pragma once

#include <cstdint>
#include <unordered_map>

#include "base/container/storage_index.h"
#include "resource/asset_type.h"
#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The previews' cache over EditorRenderHooks::renderPreview, deciding when to render.
 *
 * A key renders again only when its version changes, and thumbnails are budgeted per frame so a large
 * grid spreads over several frames; live previews skip the budget.
 */
class PreviewCache {
    public:
        explicit PreviewCache(EditorRenderHooks& hooks)
            : m_hooks(hooks) {}
        ~PreviewCache() = default;

        PreviewCache(const PreviewCache& other) = delete;
        PreviewCache& operator=(const PreviewCache& other) = delete;

        PreviewCache(PreviewCache && other) = delete;
        PreviewCache& operator=(PreviewCache && other) = delete;

    public:
        /**
         * @brief The preview key an asset's thumbnail renders under.
         *
         * @param type The asset's kind.
         * @param id   Its slot.
         * @return A key in a key space of the kind's own, never 0.
         */
        static uint64_t thumbnailKey(AssetType type, uint32_t id);

        /**
         * @brief The preview key an editor's viewport renders under.
         *
         * @param type The asset's kind.
         * @param key  Its slot.
         * @return A key no thumbnail renders under.
         */
        static uint64_t viewportKey(AssetType type, StorageIndex key);

        /**
         * @brief A live preview or cached thumbnail, rendered again only when @p version or the look changed.
         *
         * The look is EditorRenderHooks::previewLook.
         *
         * @param resources Resolves the request's meshes and materials.
         * @param req       What to draw; a thumbnail's size is the session's to set.
         * @param version   The asset's version stamp; an unchanged one reuses the cached render.
         * @param live      True for a live preview, which skips the thumbnail budget and renders at the
         *                  request's size.
         * @return The texture of the latest render for req.key, or 0 when there is nothing to show yet.
         */
        uint32_t texture(ResourceManager& resources, PreviewRequest req, uint64_t version, bool live);

        /**
         * @brief Drop the cached render for @p key; call when its asset is destroyed.
         *
         * @param key Cache key whose target and version stamp are forgotten.
         */
        void evict(uint64_t key);

        /// Drop every cached render, as when a scene load swaps the asset set out from under the cache.
        void clear();

        /// Refill the per-frame thumbnail budget.
        void onFrameBegin() { m_budget = THUMBS_PER_FRAME; }

    private:
        static constexpr int THUMBS_PER_FRAME = 2;

        static constexpr uint32_t THUMB_SIZE = 256;  ///< A thumbnail's edge, in pixels.

    private:
        EditorRenderHooks& m_hooks;

        std::unordered_map<uint64_t, uint64_t> m_versions;  ///< Each key's version at its last render.

        int m_budget = 0;
};

} // namespace Vkm::Engine
