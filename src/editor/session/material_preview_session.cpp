#include "session/material_preview_session.h"

#include "core/fnv1a.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

uint32_t MaterialPreviewSession::texture(
    ResourceManager& resources,
    PreviewRequest req,
    uint64_t version,
    bool live
) {
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
    // placeholder) meanwhile; the live pane always renders its change now.
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

void MaterialPreviewSession::evict(uint64_t key) {
    m_versions.erase(key);
    m_hooks.releasePreview(key);
}

void MaterialPreviewSession::clear() {
    m_versions.clear();
    m_hooks.releaseAllPreviews();
}

} // namespace Vkm::Engine
