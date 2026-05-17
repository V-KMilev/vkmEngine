#pragma once

#include <vector>

#include "system/render/render_graph_resource.h"

namespace Engine {

/**
 * @brief Handed to RenderPass::declareResources so a pass states its
 *        transient resource reads and writes.
 *
 * The RenderGraph creates one bound to each pass's read/write lists during
 * compile(). Declaring is cheap and side-effect free - it only records intent
 * the graph uses for validation, lifetimes, and debug.
 */
class RenderGraphBuilder {
    public:
        RenderGraphBuilder() = delete;
        ~RenderGraphBuilder() = default;

        RenderGraphBuilder(const RenderGraphBuilder& other) = delete;
        RenderGraphBuilder& operator=(const RenderGraphBuilder& other) = delete;

        RenderGraphBuilder(RenderGraphBuilder && other) = delete;
        RenderGraphBuilder& operator=(RenderGraphBuilder && other) = delete;

        RenderGraphBuilder(std::vector<RGResource>& reads, std::vector<RGResource>& writes)
            : m_reads(reads), m_writes(writes) {}

    public:
        /// Declare that the pass samples / consumes @p r.
        void read(RGResource r) { m_reads.push_back(r); }

        /// Declare that the pass produces / renders into @p r.
        void write(RGResource r) { m_writes.push_back(r); }

    private:
        std::vector<RGResource>& m_reads;
        std::vector<RGResource>& m_writes;
};

} // namespace Engine
