#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "render_pass.h"

namespace Engine {

/**
 * @brief Ordered list of render passes executed each frame.
 *
 * The RenderPipeline manages a sequence of RenderPass objects that
 * are executed during each rendering frame, in order. This allows
 * for construction of complex rendering schemes via modular passes
 * such as geometry, lighting, post-process effects, etc.
 */
class RenderPipeline {
    public:
        RenderPipeline() = default;
        ~RenderPipeline() = default;

        RenderPipeline(const RenderPipeline& other) = delete;
        RenderPipeline& operator=(const RenderPipeline& other) = delete;

        RenderPipeline(RenderPipeline && other) = delete;
        RenderPipeline& operator=(RenderPipeline && other) = delete;

    public:
        /**
         * @brief Add a render pass to the pipeline.
         *
         * @param pass Unique pointer to a RenderPass to append.
         */
        void addPass(std::unique_ptr<RenderPass> pass);

        /**
         * @brief Remove all render passes from the pipeline.
         */
        void clear();

        /**
         * @brief Respond to a resize event by updating all passes.
         *
         * @param backend Reference to current RenderBackend.
         * @param width   New framebuffer width.
         * @param height  New framebuffer height.
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height);

        /**
         * @brief Execute all render passes in the pipeline for a frame.
         *
         * @param backend   Reference to current RenderBackend.
         * @param view      RenderView providing necessary scene data.
         * @param resources ResourceManager providing GPU resource handles.
         */
        void execute(
            RenderBackend& backend,
            const RenderView& view,
            const ResourceManager& resources
        );

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
};

} // namespace Engine