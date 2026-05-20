#include "system/render/render_graph.h"

#include <algorithm>

#include "logger.h"
#include "debug/statistics.h"
#include "system/render/render_backend.h"
#include "system/render/render_graph_builder.h"
#include "system/render/render_graph_context.h"

namespace Engine {

namespace {
    bool contains(const std::vector<RGResource>& list, RGResource r) {
        return std::find(list.begin(), list.end(), r) != list.end();
    }
}

void RenderGraph::addPass(std::unique_ptr<RenderPass> pass) {
    m_passes.emplace_back(std::move(pass));
    m_compiled = false;
}

void RenderGraph::clear() {
    m_passes.clear();
    m_reads.clear();
    m_writes.clear();
    m_compiled = false;
}

void RenderGraph::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    for (auto& pass : m_passes) {
        pass->onResize(backend, width, height);
    }
}

void RenderGraph::compile() {
    const size_t n = m_passes.size();
    m_reads.assign(n, {});
    m_writes.assign(n, {});
    for (uint32_t i = 0; i < RG_RESOURCE_COUNT; ++i) m_lifetimes[i] = RGResourceLifetime{};

    bool produced[RG_RESOURCE_COUNT] = {};

    for (size_t i = 0; i < n; ++i) {
        RenderGraphBuilder builder(m_reads[i], m_writes[i]);
        m_passes[i]->declareResources(builder);

        // Reads are validated against everything produced by earlier passes.
        for (RGResource r : m_reads[i]) {
            const uint32_t ri = static_cast<uint32_t>(r);
            m_lifetimes[ri].lastRead = static_cast<int>(i);
            if (!produced[ri] && !rgResourceIsImplicit(r)) {
                LOG_WARNING("RenderGraph: pass '%s' reads %s before any pass writes it",
                    m_passes[i]->getName().c_str(), rgResourceName(r));
            }
        }
        // Then this pass's writes become available to later passes.
        for (RGResource w : m_writes[i]) {
            const uint32_t wi = static_cast<uint32_t>(w);
            if (m_lifetimes[wi].firstWrite < 0) m_lifetimes[wi].firstWrite = static_cast<int>(i);
            produced[wi] = true;
        }
    }

    uint32_t usedResources = 0;
    for (uint32_t i = 0; i < RG_RESOURCE_COUNT; ++i) {
        if (m_lifetimes[i].used()) ++usedResources;
    }
    LOG_INFO("RenderGraph compiled: %zu passes, %u transient resources", n, usedResources);

    m_compiled = true;
}

void RenderGraph::execute(
    RenderBackend& backend,
    const RenderView& view,
    const ResourceManager& resources
) {
    if (!m_compiled) compile();

    RenderGraphContext ctx{ backend, view, resources, m_frameIndex++ };

    // The graph owns the MSAA->single-sample resolve: it runs once before the
    // first pass that samples SceneHDRResolved, and again only after a later
    // pass has written SceneHDR (e.g. SSR). Passes no longer self-resolve.
    bool sceneDirty = false;

    for (size_t i = 0; i < m_passes.size(); ++i) {
        RenderPass& pass = *m_passes[i];
        if (!pass.isEnabled()) continue;

        if (sceneDirty && contains(m_reads[i], RGResource::SceneHDRResolved)) {
            backend.resolveSceneColor();
            sceneDirty = false;
        }

        pass.execute(ctx);
        STATS_RECORD_RENDER_PASS();

        if (contains(m_writes[i], RGResource::SceneHDR)) {
            sceneDirty = true;
        }
    }
}

} // namespace Engine
