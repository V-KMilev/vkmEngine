#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_system.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "logger.h"

#include "core/host_chrome.h"
#include "platform/window/window_manager.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "debug/profiler.h"
#include "debug/screenshot.h"

namespace Vkm::Engine {

void RenderSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("RenderSystem");

    if (!m_backend || !ctx.visibility) return;

    const HostChrome::ViewportRect vp = ctx.chrome.viewport(ctx.window);
    m_view.viewportX      = vp.x;
    m_view.viewportY      = vp.y;
    m_view.viewportWidth  = vp.width;
    m_view.viewportHeight = vp.height;
    m_view.surfaceWidth   = static_cast<uint32_t>(ctx.window.getWidth());
    m_view.surfaceHeight  = static_cast<uint32_t>(ctx.window.getHeight());
    m_view.settings       = ctx.render;

    m_view.build(ctx.scene, *ctx.visibility, ctx.ui, ctx.splash, ctx.poses, ctx.particles);
    m_backend->render(m_view, ctx.resources);
    if (std::string path = ctx.window.takeScreenshotRequest(); !path.empty()) writeScreenshot(path);
    if (m_releaseUploadedPixels) releaseHeldPixels(ctx.resources);
}

void RenderSystem::writeScreenshot(const std::string& path) {
    std::vector<uint8_t> frame;
    if (!m_backend->readFrame(m_view, frame)) {
        LOG_WARNING(
            "Screenshot not written to %s: the render backend cannot read its frame back",
            path.c_str()
        );
        return;
    }
    if (writeScreenshotPng(path, m_view.viewportWidth, m_view.viewportHeight, frame)) {
        LOG_INFO("Screenshot written to %s", path.c_str());
    } else {
        LOG_WARNING("Screenshot could not be written to %s", path.c_str());
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

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend, WindowManager& window) {
    if (!backend || !backend->init(window)) throw std::runtime_error("The render backend failed to init");
    m_backend = std::move(backend);
    LOG_INFO("Render backend active: %s", m_backend->info().api.c_str());
}

} // namespace Vkm::Engine
