#pragma once

#include <cstdint>

#include "system/render/render_graph_resource.h"

namespace Engine {
    class RenderBackend;
    struct RenderView;
    class ResourceManager;
    class RenderGraph;
}

namespace Engine {

/**
 * @brief Per-frame execution context handed to every render pass.
 *
 * Bundles what a pass needs from the graph: the backend, the frame's
 * RenderView snapshot, the ResourceManager, the owning RenderGraph (for
 * typed resource lookup), and a monotonically increasing frame index
 * (used by temporal effects - TAA jitter / history selection).
 *
 * Passes still downcast `backend` to their concrete type for persistent
 * state (GL context, GLView, default render target). Per-frame transient
 * resources go through @ref resource() so the graph's lifetime tracking
 * connects to a real lookup.
 */
struct RenderGraphContext {
    RenderBackend&          backend;
    const RenderView&       view;
    const ResourceManager&  resources;
    RenderGraph&            graph;
    uint64_t                frameIndex = 0;

    /// Typed access to a graph-registered resource. Returns nullptr when
    /// the backend hasn't published @p id this frame. The caller is
    /// responsible for picking @p T to match the concrete type the
    /// backend registered (OpenGL backend registers GLHdrTarget* for
    /// SceneHDR, GLBloom* for BloomChain, etc.).
    template<typename T>
    T* resource(RGResource id) const {
        return static_cast<T*>(graph.getResource(id));
    }
};

} // namespace Engine

#include "system/render/render_graph.h"
