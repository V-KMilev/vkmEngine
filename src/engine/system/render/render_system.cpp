#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_system.h"

#include "logger.h"

#include "platform/window/window_manager.h"
#include "system/render/render_backend.h"
#include "debug/profiler.h"

namespace Engine {

void RenderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("RenderSystem");

    installPending(ctx);
    if (!m_backend || !ctx.visibility) return;

    // Resize the backend's surface only when the viewport actually changes.
    // m_view holds the size we last rendered at; installPending() zeroes it
    // after a swap so a freshly installed backend is sized on its first frame.
    const uint32_t vpX = ctx.window.sceneViewportX();
    const uint32_t vpY = ctx.window.sceneViewportY();
    const uint32_t vpW = ctx.window.sceneViewportWidth();
    const uint32_t vpH = ctx.window.sceneViewportHeight();
    if (vpX != m_view.viewportX || vpY != m_view.viewportY ||
        vpW != m_view.viewportWidth || vpH != m_view.viewportHeight) {
        m_view.viewportX      = vpX;
        m_view.viewportY      = vpY;
        m_view.viewportWidth  = vpW;
        m_view.viewportHeight = vpH;
        m_backend->resize(vpX, vpY, vpW, vpH);
    }

    // The full backbuffer height lets a bottom-left backend flip the top-left
    // viewport rect. Refreshed every frame so a window resize that leaves the
    // rect unchanged still lands the blit correctly.
    m_view.surfaceHeight = static_cast<uint32_t>(ctx.window.getHeight());
    m_view.settings      = m_settings;

    m_view.build(ctx.scene, *ctx.visibility);
    m_backend->render(m_view, ctx.resources);
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend) {
    if (!backend) {
        LOG_WARNING("setBackend called with null backend; ignoring");
        return;
    }
    // Queue it; the swap happens at the top of the next update(), where we have
    // the window to bring it up against. Lets startup and a runtime hot-swap
    // share one code path.
    m_pending = std::move(backend);
}

void RenderSystem::installPending(FrameContext& ctx) {
    if (!m_pending) return;

    // Bring the incoming backend up first. Only on success do we replace the
    // current one (whose destructor then frees its GPU state); a backend that
    // fails to init is dropped and the current one keeps rendering.
    if (m_pending->init(ctx.window)) {
        m_backend = std::move(m_pending);
        // force a resize onto the new backend: zero the cached viewport so the
        // next update()'s change check fires.
        m_view.viewportX      = 0;
        m_view.viewportY      = 0;
        m_view.viewportWidth  = 0;
        m_view.viewportHeight = 0;
        LOG_INFO("Render backend active: %s", m_backend->info().api.c_str());
    } else {
        LOG_ERROR("Incoming render backend failed to init; keeping the current one");
        m_pending.reset();
    }
}

} // namespace Engine
