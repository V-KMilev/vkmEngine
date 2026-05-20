#pragma once

#include <cstdint>

namespace Engine {

class RenderGraph;

/**
 * @brief Backend-agnostic handle for the graph's viewport-sized transient
 *        GPU resource pool.
 *
 * Concrete pools (GLFrameResources for OpenGL; a future GLVkFrameResources
 * for Vulkan) inherit from this and add their backend-typed accessors. The
 * engine-side RenderGraph holds one of these via unique_ptr and reaches it
 * through this thin interface — engine code never #includes a backend
 * header.
 *
 * Lifecycle:
 *   1. RenderGraph asks the active RenderBackend to construct one
 *      via @ref RenderBackend::createFrameResources().
 *   2. RenderGraph calls resize() whenever the viewport dimensions change.
 *   3. RenderGraph calls registerWith() at the top of each execute() so
 *      the graph's typed-resource pool sees the active set (preview mode
 *      swaps in a private FrameResources via the same path).
 */
class FrameResources {
    public:
        virtual ~FrameResources() = default;

        FrameResources(const FrameResources& other) = delete;
        FrameResources& operator=(const FrameResources& other) = delete;

        FrameResources(FrameResources && other) = delete;
        FrameResources& operator=(FrameResources && other) = delete;

        /**
         * @brief Reallocate the pool's targets to (width, height).
         *
         * Implementations should be idempotent against repeat calls at the
         * same size. After resize the storage pointers may move; the graph
         * re-registers the pool via @ref registerWith() so passes pick up
         * the new addresses.
         */
        virtual void resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief Publish every sub-resource into @p graph's typed pool,
         *        keyed by the appropriate RGResource id.
         */
        virtual void registerWith(RenderGraph& graph) = 0;

        /**
         * @brief MSAA-resolve the scene-HDR target into its single-sample
         *        copy. No-op for backends without MSAA. Called by
         *        @ref RenderGraph::execute() between a pass that writes
         *        SceneHDR and the next pass that reads SceneHDRResolved.
         */
        virtual void resolveSceneColor() = 0;

    protected:
        FrameResources() = default;
};

} // namespace Engine
