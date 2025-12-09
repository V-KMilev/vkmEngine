#include "render_manager.h"

#include "logger.h"

#include "render_backend.h"

#include "render_view_builder.h"
#include "resource_manager.h"

#include "scene.h"

namespace Engine {

RenderManager::RenderManager() : m_width(0), m_height(0) {}

void RenderManager::setBackend(std::unique_ptr<RenderBackend> backend) {
    m_backend = std::move(backend);

    // If we already know size, propagate it
    if (m_backend && m_width > 0 && m_height > 0) {
        m_backend->resize(m_width, m_height);
        m_pipeline.onResize(*m_backend, m_width, m_height);
    }
}

void RenderManager::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!m_backend) return;

    m_backend->resize(width, height);
    m_pipeline.onResize(*m_backend, width, height);
}

void RenderManager::renderFrame(
    const Scene& scene,
    const ResourceManager& resources,
    uint32_t viewportWidth,
    uint32_t viewportHeight
) {
    if (!m_backend) {
        LOG_WARNING("No backend set, skipping render frame");
        return;
    }

    // Keep internal size in sync
    if (viewportWidth != m_width || viewportHeight != m_height) {
        resize(viewportWidth, viewportHeight);
    }

    // Build snapshot for this frame
    RenderView view = RenderViewBuilder::build(scene);

    // Execute passes
    m_pipeline.execute(*m_backend, view, resources);
}

void RenderManager::addPass(std::unique_ptr<RenderPass> pass) {
    m_pipeline.addPass(std::move(pass));
}

void RenderManager::clearPasses() {
    m_pipeline.clear();
}

} // namespace Engine