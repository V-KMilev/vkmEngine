#pragma once

#include <cstdint>

#include "system/render/render_graph.h"
#include "system/render/render_graph_resource.h"

namespace Engine {

class RenderBackend;
struct RenderView;
class ResourceManager;

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
 *
 * In debug builds, every `resource<T>(id)` call records the id into
 * @ref accessedResources. RenderGraph::execute() compares this against
 * the pass's declared reads/writes after the pass returns and warns
 * about drift in either direction (declared but not accessed, or
 * accessed but not declared). Release builds skip the tracking entirely.
 */
struct RenderGraphContext {
    RenderBackend&          backend;
    const RenderView&       view;
    const ResourceManager&  resources;
    RenderGraph&            graph;
    uint64_t                frameIndex = 0;

#ifndef NDEBUG
    /**
     * @brief Debug-only bitmask of RGResource ids actually looked up during the current pass.
     *
     * Cleared by RenderGraph::execute before each pass, checked after the
     * pass returns. Mutable so resource<T>() can stay const-correct from
     * the pass's point of view.
     */
    mutable uint32_t accessedResources = 0;
#endif

    /**
     * @brief Typed access to a graph-registered resource.
     *
     * Returns nullptr when the backend hasn't published @p id this frame.
     * The caller is responsible for picking @p T to match the concrete
     * type the backend registered (OpenGL backend registers GLSceneTarget*
     * for SceneHDR, GLBloom* for BloomChain, etc.).
     */
    template<typename T>
    T* resource(RGResource id) const {
#ifndef NDEBUG
        accessedResources |= (1u << static_cast<uint32_t>(id));
#endif
        return static_cast<T*>(graph.getResource(id));
    }
};

} // namespace Engine
