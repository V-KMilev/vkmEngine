#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "system/render/render_pass.h"
#include "system/render/render_graph_resource.h"

namespace Engine {

/**
 * @brief First write / last read pass indices for a transient resource.
 *
 * Drives future pool aliasing (two resources with disjoint lifetimes can
 * share storage) and the editor debug view. -1 means never written / read.
 */
struct RGResourceLifetime {
    int firstWrite = -1;
    int lastRead   = -1;

    bool used() const { return firstWrite >= 0 || lastRead >= 0; }
};

/**
 * @brief Typed render graph - an ordered pass list plus the resource flow.
 *
 * Supersedes RenderPipeline: same ordered execution, but each pass declares
 * the transient resources it reads/writes (RenderPass::declareResources).
 * compile() validates ordering (read-before-write), computes per-resource
 * lifetimes, and exposes both for debug. Concrete GPU storage still lives in
 * the backend for now; moving it into a graph-owned, lifetime-aliased pool
 * (with auto-inserted resolves) is the next step and does not change this API.
 */
class RenderGraph {
    public:
        RenderGraph() = default;
        ~RenderGraph() = default;

        RenderGraph(const RenderGraph& other) = delete;
        RenderGraph& operator=(const RenderGraph& other) = delete;

        RenderGraph(RenderGraph && other) = delete;
        RenderGraph& operator=(RenderGraph && other) = delete;

    public:
        void addPass(std::unique_ptr<RenderPass> pass);
        void clear();

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height);
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources);

        /// Collect declarations, validate ordering, compute lifetimes.
        /// Idempotent; auto-invoked by execute() when the pass set changed.
        void compile();

    public:
        size_t passCount() const { return m_passes.size(); }
        RenderPass& getPass(size_t index) { return *m_passes[index]; }
        const RenderPass& getPass(size_t index) const { return *m_passes[index]; }

        const RGResourceLifetime& lifetime(RGResource r) const {
            return m_lifetimes[static_cast<uint32_t>(r)];
        }
        const std::vector<RGResource>& passReads(size_t index)  const { return m_reads[index]; }
        const std::vector<RGResource>& passWrites(size_t index) const { return m_writes[index]; }

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
        std::vector<std::vector<RGResource>>     m_reads;
        std::vector<std::vector<RGResource>>     m_writes;
        RGResourceLifetime                       m_lifetimes[RG_RESOURCE_COUNT];
        bool                                     m_compiled = false;
        uint64_t                                 m_frameIndex = 0;
};

} // namespace Engine
