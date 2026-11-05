#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_system.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vkm_log/logger.h"

#include "core/graphics.h"
#include "core/host_chrome.h"
#include "core/input_map.h"
#include "vkm_platform/window/window_manager.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "debug/draw/debug_draw.h"
#include "base/profile/profiler.h"
#include "core/clock.h"
#include "system/render/screenshot.h"
#include "net/net_session.h"

namespace Vkm::Engine {

void RenderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("RenderSystem");

    if (!m_backend || !ctx.visibility) {
        ctx.readiness = m_backend ? m_backend->readiness() : RenderReadiness{};
        return;
    }

    const HostChrome::ViewportRect vp = ctx.chrome.viewportIn(ctx.window);
    m_view.viewportX      = vp.x;
    m_view.viewportY      = vp.y;
    m_view.viewportWidth  = vp.width;
    m_view.viewportHeight = vp.height;
    m_view.surfaceWidth   = static_cast<uint32_t>(ctx.window.getWidth());
    m_view.surfaceHeight  = static_cast<uint32_t>(ctx.window.getHeight());
    m_view.settings       = &ctx.graphics.settings();
    m_view.display        = ctx.chrome.display;
    m_view.frameSeconds   = ctx.clock.getDeltaTime();
    m_view.bakeBudget     = RENDER_BAKE_BUDGET;
    m_view.worldSeconds   = worldTime(ctx.net, ctx.clock);
    m_view.worldArriving  = m_bakeHold.holds(ctx.scene, ctx.resources, ctx.readiness.skyPending);
    m_view.loading        = ctx.loading;
    m_view.pointer        = ctx.input.pointer() * ctx.window.framebufferScale();
    m_view.lists.debug    = {};
    if (ctx.debug) {
        // Last of the frame's drawers but the editor, whose lines join the next frame's.
        ctx.debug->collect(ctx.clock.getSimDelta(), ctx.scene.epoch());
        m_view.lists.debug.lines      = &ctx.debug->lines();
        m_view.lists.debug.pointScale = ctx.window.framebufferScale() * ctx.debug->pointScale();
    }

    m_view.build(ctx.scene, *ctx.visibility, ctx.ui, ctx.cover, ctx.splash, ctx.poses, ctx.particles);
    m_backend->render(m_view, ctx.resources);
    // Once drawn, so the next frame's systems read what this one left owed: a splash image it began.
    ctx.readiness = m_backend->readiness();
    if (std::string path = ctx.graphics.takeScreenshotRequest(); !path.empty()) {
        readScreenshot(std::move(path));
    }
    writeLandedScreenshots(false);
    if (m_releaseUploadedPixels) releaseHeldPixels(ctx.resources);
}

void RenderSystem::shutdown() {
    if (!m_backend) return;
    writeLandedScreenshots(true);
    m_screenshots.finish();
}

void RenderSystem::readScreenshot(std::string path) {
    if (!m_backend->readFrame(m_view)) {
        LOG_WARNING("Screenshot not written to %s: the viewport is empty", path.c_str());
        return;
    }
    m_reading.push_back(std::move(path));
}

void RenderSystem::writeLandedScreenshots(bool wait) {
    FramePixels frame;
    while (!m_reading.empty() && m_backend->takeFrame(frame, wait)) {
        m_screenshots.write(std::move(m_reading.front()), std::move(frame));
        m_reading.pop_front();
    }
}

void RenderSystem::releaseHeldPixels(ResourceManager& resources) const {
    // Gathered first: the walk iterates the storage the edits write to.
    std::vector<TextureHandle> held;
    resources.forEachOfType<TextureAsset>([&](TextureHandle handle, const TextureAsset& texture) {
        if (texture.loading || texture.pixelData.empty()) return;
        if (m_backend->holdsPixels(handle, texture.version())) held.push_back(handle);
    });
    // Not a commit: the version must still match the backend's copy.
    for (const TextureHandle& handle : held) std::vector<uint8_t>().swap(resources.edit(handle).pixelData);
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend, Platform::WindowManager& window) {
    if (!backend || !backend->init(window)) throw std::runtime_error("The render backend failed to init");
    m_backend = std::move(backend);
    LOG_INFO("Render backend active: %s", m_backend->info().api.c_str());
}

} // namespace Vkm::Engine
