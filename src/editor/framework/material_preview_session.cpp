#include "framework/material_preview_session.h"

#include "system/render/render_backend.h"
#include "system/render/render_system.h"

namespace Engine {

uint32_t MaterialPreviewSession::texture(
    ResourceManager& resources,
    PreviewRequest req,
    uint64_t version,
    bool live
) {
    RenderBackend* backend = m_renderSystem.backend();
    if (!backend || !req.material || !req.mesh) return 0;

    const uint32_t cached = backend->previewTexture(req.key);
    const auto it = m_versions.find(req.key);
    if (cached && it != m_versions.end() && it->second == version) {
        return cached;  // up to date
    }

    // Thumbnails wait their budget turn and show the stale image (or the "..."
    // placeholder) meanwhile; the live pane always renders its change now.
    if (!live) {
        if (m_budget <= 0) return cached;
        --m_budget;
    }

    req.size = live ? LIVE_SIZE : THUMB_SIZE;

    const uint32_t tex = backend->renderPreview(req, resources);
    if (tex) m_versions[req.key] = version;
    return tex ? tex : cached;
}

void MaterialPreviewSession::evict(uint64_t key) {
    m_versions.erase(key);
    if (RenderBackend* backend = m_renderSystem.backend()) {
        backend->releasePreview(key);
    }
}

void MaterialPreviewSession::clear() {
    m_versions.clear();
    if (RenderBackend* backend = m_renderSystem.backend()) {
        backend->releaseAllPreviews();
    }
}

} // namespace Engine
