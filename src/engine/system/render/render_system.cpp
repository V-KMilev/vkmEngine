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

    // The rect the scene draws into, plus the full backbuffer height a
    // bottom-left backend needs to flip it. The view is the only channel that
    // carries them, so they are simply refreshed every frame.
    m_view.viewportX      = ctx.window.sceneViewportX();
    m_view.viewportY      = ctx.window.sceneViewportY();
    m_view.viewportWidth  = ctx.window.sceneViewportWidth();
    m_view.viewportHeight = ctx.window.sceneViewportHeight();
    m_view.surfaceHeight  = static_cast<uint32_t>(ctx.window.getHeight());
    m_view.settings       = m_settings;

    m_view.build(ctx.scene, *ctx.visibility, ctx.ui);
    m_backend->render(m_view, ctx.resources);
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend) {
    if (!backend) {
        LOG_WARNING("setBackend called with null backend; ignoring");
        return;
    }
    // The swap needs the window, which only update() has. Queueing it lets
    // startup and a runtime hot-swap share one code path.
    m_pending = std::move(backend);
}

void RenderSystem::installPending(FrameContext& ctx) {
    if (!m_pending) return;

    // Bring the incoming backend up before replacing the current one (whose
    // destructor then frees its GPU state), so a backend that fails to init is
    // dropped and the current one keeps rendering.
    if (m_pending->init(ctx.window)) {
        m_backend = std::move(m_pending);
        LOG_INFO("Render backend active: %s", m_backend->info().api.c_str());
    } else {
        LOG_ERROR("Incoming render backend failed to init; keeping the current one");
        m_pending.reset();
    }
}

} // namespace Engine
