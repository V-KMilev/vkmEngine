#include "render_manager.h"

#include "logger.h"

#include "render_view_builder.h"
#include "resource_manager.h"
#include "scene.h"
#include "scene_view.h"

#include "render_backend.h"
#include "render_pass.h"
#include "entity.h"

namespace Engine {

RenderManager::RenderManager()
    : m_width(0)
    , m_height(0)
{
}

RenderManager::~RenderManager() {
    m_backend.reset();
}

void RenderManager::setBackend(std::unique_ptr<RenderBackend> backend) { m_backend = std::move(backend); }

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
    const std::vector<EntityId>& visibleIds,
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

    // Build snapshot for this frame using provided visible IDs
    RenderView view = RenderViewBuilder::build(scene, resources, visibleIds);

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
