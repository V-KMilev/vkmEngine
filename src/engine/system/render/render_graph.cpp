#include "system/render/render_graph.h"

#include <algorithm>

#include "logger.h"
#include "debug/statistics.h"
#include "system/render/frame_resources.h"
#include "system/render/render_backend.h"
#include "system/render/render_graph_builder.h"
#include "system/render/render_graph_context.h"
#include "system/render/render_pass.h"
#include "system/render/render_target.h"

namespace Engine {

RenderGraph::RenderGraph()  = default;
RenderGraph::~RenderGraph() = default;

RenderPass& RenderGraph::getPass(size_t index) {
    return *m_passes[index];
}

const RenderPass& RenderGraph::getPass(size_t index) const {
    return *m_passes[index];
}

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
    m_width  = width;
    m_height = height;
    // Lazy-create the default pool on first resize; resize the existing pool
    // on subsequent calls. The backend factory may return nullptr while the
    // backend is still mid-setup (rare); guard so we don't crash here.
    FrameResources& f = ensureFrame(backend);
    f.resize(width, height);
    for (auto& pass : m_passes) {
        pass->onResize(backend, width, height);
    }
}

FrameResources& RenderGraph::ensureFrame(RenderBackend& backend) {
    if (!m_frame) {
        m_frame = backend.createFrameResources();
    }
    return *m_frame;
}

FrameResources& RenderGraph::activeFrame() const {
    return m_previewActive && m_previewFrame ? *m_previewFrame : *m_frame;
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

    // Refresh the resource pool from the active FrameResources (default or
    // preview). registerWith() re-publishes every sub-resource into the
    // typed pool, so a preview swap just needs an extra call into this path.
    FrameResources& f = m_previewActive && m_previewFrame
        ? *m_previewFrame : ensureFrame(backend);
    f.registerWith(*this);

    // Persistent resources (ShadowAtlas, IBL) are published once - they
    // outlive the frame and don't change across previews.
    if (!m_persistentRegistered) {
        backend.registerPersistentResources(*this);
        m_persistentRegistered = true;
    }

    // Backbuffer routes through the typed pool too. Preview sessions point
    // it at the offscreen target the graph owns; otherwise it's the window
    // backbuffer the backend hands us.
    RenderTarget* target = (m_previewActive && m_previewTarget)
        ? m_previewTarget.get()
        : &backend.getDefaultTarget();
    registerResource(RGResource::Backbuffer, target);

    RenderGraphContext ctx{ backend, view, resources, *this, m_frameIndex++ };

    // The graph owns the MSAA->single-sample resolve: it runs once before the
    // first pass that samples SceneHDRResolved, and again only after a later
    // pass has written SceneHDR (e.g. SSR). Passes no longer self-resolve.
    bool sceneDirty = false;

    for (size_t i = 0; i < m_passes.size(); ++i) {
        RenderPass& pass = *m_passes[i];
        if (!pass.isEnabled()) continue;

        if (sceneDirty && contains(m_reads[i], RGResource::SceneHDRResolved)) {
            f.resolveSceneColor();
            sceneDirty = false;
        }

        pass.execute(ctx);
        STATS_RECORD_RENDER_PASS();

        if (contains(m_writes[i], RGResource::SceneHDR)) {
            sceneDirty = true;
        }
    }
}

void RenderGraph::beginPreview(RenderBackend& backend, uint32_t size) {
    if (size == 0) return;
    if (!m_previewFrame || m_previewSize != size) {
        m_previewFrame = backend.createFrameResources();
        m_previewTarget = backend.createOffscreenTarget(size);
        m_previewSize = size;
    }
    if (m_previewFrame) m_previewFrame->resize(size, size);
    m_previewActive = true;
}

void RenderGraph::endPreview() {
    m_previewActive = false;
    // Keep the preview pool + target around for reuse on the next session.
    // Note: the composite pass left the offscreen FBO bound. Callers (e.g.
    // RenderSystem.renderMaterialPreview) need to rebind the backbuffer
    // before subsequent UI rendering, or ImGui draws into the preview FBO.
}

uint32_t RenderGraph::previewColorTexture() const {
    if (!m_previewTarget) return 0u;
    return m_previewTarget->getColorTexture();
}

uint32_t RenderGraph::snapshotPreviewToCache(RenderBackend& backend,
                                             uint64_t key, uint32_t size) {
    if (!m_previewTarget) return 0u;
    const uint32_t srcId = m_previewTarget->getColorTexture();
    if (srcId == 0u) return 0u;
    const uint32_t id = backend.snapshotToTexture(srcId, key, size);
    if (id != 0u) m_thumbCache[key] = id;
    return id;
}

uint32_t RenderGraph::cachedPreview(RenderBackend& backend, uint64_t key) const {
    auto it = m_thumbCache.find(key);
    if (it != m_thumbCache.end()) return it->second;
    return backend.cachedThumbnail(key);
}

} // namespace Engine
