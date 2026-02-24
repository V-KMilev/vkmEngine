#include "system/render/render_pipeline.h"
#include "debug/statistics.h"

namespace Engine {

void RenderPipeline::addPass(std::unique_ptr<RenderPass> pass) {
    m_passes.emplace_back(std::move(pass));
}

void RenderPipeline::clear() {
    m_passes.clear();
}

void RenderPipeline::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    for (auto& pass : m_passes) {
        pass->onResize(backend, width, height);
    }
}

void RenderPipeline::execute(
    RenderBackend& backend,
    const RenderView& view,
    const ResourceManager& resources
) {
    for (auto& pass : m_passes) {
        if (!pass->isEnabled()) continue;
        pass->execute(backend, view, resources);
        STATS_RECORD_RENDER_PASS();
    }
}

} // namespace Engine
