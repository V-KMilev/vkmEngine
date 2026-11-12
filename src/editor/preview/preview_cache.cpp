#include "preview/preview_cache.h"

#include "base/hash/fnv1a.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t KIND_SHIFT = 40;  ///< Where a preview key's kind starts: below it, the asset's slot.

constexpr uint64_t VIEWPORT_KEY_BIT = 1ull << 56;  ///< The bit no thumbnail's key sets.
static_assert(
    ((static_cast<uint64_t>(AssetType::Count) + 1ull) << KIND_SHIFT) < VIEWPORT_KEY_BIT,
    "A thumbnail's key stops below the viewports'"
);

} // namespace

uint64_t PreviewCache::thumbnailKey(AssetType type, uint32_t id) {
    return ((static_cast<uint64_t>(type) + 1ull) << KIND_SHIFT) | id;
}

uint64_t PreviewCache::viewportKey(AssetType type, StorageIndex key) {
    return VIEWPORT_KEY_BIT | (static_cast<uint64_t>(type) << KIND_SHIFT) | key.index;
}

uint32_t PreviewCache::texture(ResourceManager& resources, PreviewRequest req, uint64_t version, bool live) {
    if (req.draws.empty()) return 0;
    for (const PreviewDraw& draw : req.draws) {
        if (!draw.material || !draw.mesh) return 0;
    }

    // The scene's light is part of the picture, as the asset is.
    version = fnv1a64Bytes(&version, sizeof(version), m_hooks.previewLook());

    // And the materials' maps: one whose bake lands after the picture was taken is a new picture.
    const auto fold = [&](TextureHandle map) {
        const TextureAsset* texture = map ? resources.tryGet(map) : nullptr;
        const uint64_t stamp = texture ? texture->version() + 1 : 0;
        version = fnv1a64Bytes(&stamp, sizeof(stamp), version);
    };
    for (const PreviewDraw& draw : req.draws) {
        const MaterialAsset* material = resources.tryGet(draw.material);
        if (!material) continue;
#define VKM_PREVIEW_FOLD_MAP(key, member, slot, doc) fold(material->member);
        VKM_MATERIAL_MAPS(VKM_PREVIEW_FOLD_MAP)
#undef VKM_PREVIEW_FOLD_MAP
    }

    const GpuTextureId cached = m_hooks.previewTexture(req.key);
    const auto it = m_versions.find(req.key);
    if (cached && it != m_versions.end() && it->second == version) {
        return cached;
    }

    // Thumbnails wait their budget turn and show the stale image (or the "..."
    // placeholder) meanwhile; the live pane always renders its change at once.
    if (!live) {
        if (m_budget <= 0) return cached;
        --m_budget;
    }

    if (!live) {
        req.width  = THUMB_SIZE;
        req.height = THUMB_SIZE;
    }

    const GpuTextureId tex = m_hooks.renderPreview(req, resources);
    if (tex) m_versions[req.key] = version;
    return tex ? tex : cached;
}

void PreviewCache::evict(uint64_t key) {
    m_versions.erase(key);
    m_hooks.releasePreview(key);
}

void PreviewCache::clear() {
    m_versions.clear();
    m_hooks.releaseAllPreviews();
}

} // namespace Vkm::Engine
