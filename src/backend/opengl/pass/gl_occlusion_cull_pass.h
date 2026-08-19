#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class ComputeShader;
}

namespace Vkm::Engine {

/**
 * @brief Culls hidden instances against the Hi-Z pyramid, on the GPU.
 *
 * Runs between the pyramid build and the passes that draw the opaque bucket.
 * One invocation per instance tests its world AABB against the frame's depth
 * and, if it survives, appends itself to its run's slice of the compacted
 * instance buffers while bumping that run's indirect draw command.
 *
 * The answer never comes back to the CPU. That is the whole point: a readback
 * would stall the frame it is trying to speed up, and the draw count is
 * unchanged either way - the GPU decides how many instances each run draws, not
 * how many draws there are.
 */
class GLOcclusionCullPass : public GLPass {
    public:
        GLOcclusionCullPass();
        ~GLOcclusionCullPass() override;

        GLOcclusionCullPass(const GLOcclusionCullPass& other) = delete;
        GLOcclusionCullPass& operator=(const GLOcclusionCullPass& other) = delete;

        GLOcclusionCullPass(GLOcclusionCullPass&& other) = delete;
        GLOcclusionCullPass& operator=(GLOcclusionCullPass&& other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::ComputeShader> m_compute;
};

} // namespace Vkm::Engine
