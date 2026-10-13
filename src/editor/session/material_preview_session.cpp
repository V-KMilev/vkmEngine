#include "session/material_preview_session.h"

#include "core/fnv1a.h"
#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

uint32_t MaterialPreviewSession::texture(
    ResourceManager& resources,
    PreviewRequest req,
    uint64_t version,
    bool live
) {
    if (!req.material || !req.mesh) return 0;

    // The scene's light is part of the picture, as the asset is.
    version = fnv1a64Bytes(&version, sizeof(version), m_hooks.previewLook());

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

    req.size = live ? LIVE_SIZE : THUMB_SIZE;

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
