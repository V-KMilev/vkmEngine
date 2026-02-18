#include "render/render_system.h"

#include "logger.h"

#include "render/render_view.h"
#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "visibility/visibility.h"

#include "render/render_backend.h"
#include "render/render_pass.h"

namespace Engine {

RenderSystem::RenderSystem() : m_width(0), m_height(0) {}

RenderSystem::~RenderSystem() {
    m_backend.reset();
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend) { m_backend = std::move(backend); }

void RenderSystem::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!m_backend) return;

    m_backend->resize(width, height);
    m_pipeline.onResize(*m_backend, width, height);
}

void RenderSystem::update(FrameContext& ctx) {
    if (!m_backend) {
        LOG_WARNING("No backend set, skipping render frame");
        return;
    }

    // Keep internal size in sync
    if (ctx.viewportWidth != m_width || ctx.viewportHeight != m_height) {
        resize(ctx.viewportWidth, ctx.viewportHeight);
    }

    // Build snapshot for this frame
    RenderView view = RenderView::build(ctx.scene, ctx.resources, ctx.visibility);

    // Execute passes
    m_pipeline.execute(*m_backend, view, ctx.resources);
}

void RenderSystem::addPass(std::unique_ptr<RenderPass> pass) {
    m_pipeline.addPass(std::move(pass));
}

void RenderSystem::clearPasses() {
    m_pipeline.clear();
}

} // namespace Engine
