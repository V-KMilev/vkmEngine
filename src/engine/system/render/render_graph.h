#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "system/render/render_graph_resource.h"

namespace Engine {

class RenderPass;       // defined in render_pass.h
class RenderBackend;    // defined in render_backend.h
struct RenderView;      // defined in render_view.h
class ResourceManager;
}

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
 * compile() validates ordering (read-before-write) and computes per-resource
 * lifetimes (firstWrite -> lastRead), exposed via lifetime() for debug.
 *
 * STATE OF THE GRAPH (long-term roadmap):
 *
 *   What it owns today:
 *     - The pass list and execution order.
 *     - The compile-time validation of resource ordering.
 *     - The MSAA -> single-sample resolve hook between writes to SceneHDR
 *       and reads of SceneHDRResolved.
 *
 *   What still lives on the backend (the next steps):
 *     - The concrete GPU storage for each RGResource (FrameResources
 *       inside GLBackend - HDR target, bloom chain, GBuffer, etc.).
 *     - Passes still reach for backend.getHdrTarget() / getBloom() / ...
 *       at execute time instead of resolving by RGResource enum.
 *
 *   Migration plan when the next step lands:
 *     1. Move the FrameResources pool into the graph itself, keyed by
 *        RGResource enum. Backend still allocates the underlying GL
 *        objects but hands them to the graph at resize() time.
 *     2. RenderGraphContext gains a getResource<T>(RGResource) accessor.
 *     3. Passes use that accessor instead of backend-typed getters. The
 *        existing read/write declarations stay the same.
 *     4. With ownership in the graph, lifetime aliasing (two resources
 *        with disjoint [firstWrite..lastRead] sharing storage) becomes a
 *        natural follow-up - memory budget knob without changing passes.
 *
 *   None of this changes the RenderGraph public API; passes that don't
 *   touch backend-typed resources never need an edit.
 */
class RenderGraph {
    public:
        RenderGraph();
        ~RenderGraph();

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
        RenderPass& getPass(size_t index);
        const RenderPass& getPass(size_t index) const;

        const RGResourceLifetime& lifetime(RGResource r) const {
            return m_lifetimes[static_cast<uint32_t>(r)];
        }
        const std::vector<RGResource>& passReads(size_t index)  const { return m_reads[index]; }
        const std::vector<RGResource>& passWrites(size_t index) const { return m_writes[index]; }

        /// Register the concrete backend object that backs a logical
        /// resource id. The backend calls this from
        /// `RenderBackend::populateGraphResources(*this)` at the top of
        /// every execute() so the active set is up-to-date - in particular
        /// the editor's preview path swaps in a private FrameResources and
        /// needs the pool to repoint without recompiling the graph.
        ///
        /// Storage is type-erased; passes downcast via the typed accessor
        /// `RenderGraphContext::resource<T>(id)`.
        void registerResource(RGResource id, void* ptr) {
            m_resources[static_cast<uint32_t>(id)] = ptr;
        }

        /// Raw resource pointer. Prefer `RenderGraphContext::resource<T>()`
        /// at pass call sites; this is the low-level accessor.
        void* getResource(RGResource id) const {
            return m_resources[static_cast<uint32_t>(id)];
        }

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
        std::vector<std::vector<RGResource>>     m_reads;
        std::vector<std::vector<RGResource>>     m_writes;
        RGResourceLifetime                       m_lifetimes[RG_RESOURCE_COUNT];
        void*                                    m_resources[RG_RESOURCE_COUNT] = {};
        bool                                     m_compiled = false;
        uint64_t                                 m_frameIndex = 0;
};

} // namespace Engine
