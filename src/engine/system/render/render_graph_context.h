#pragma once

#include <cstdint>

namespace Engine {
    class RenderBackend;
    struct RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Per-frame execution context handed to every render pass.
 *
 * Bundles what a pass needs from the graph: the backend, the frame's
 * RenderView snapshot, the ResourceManager, and a monotonically increasing
 * frame index (used by temporal effects - TAA jitter / history selection).
 * Passes still downcast `backend` to their concrete backend for GPU access;
 * the graph owns the transient resource pool behind it.
 */
struct RenderGraphContext {
    RenderBackend&          backend;
    const RenderView&       view;
    const ResourceManager&  resources;
    uint64_t                frameIndex = 0;
};

} // namespace Engine
