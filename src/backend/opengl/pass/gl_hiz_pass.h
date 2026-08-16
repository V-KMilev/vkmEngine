#pragma once

#include <memory>

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Builds the frame's hierarchical depth pyramid from the resolved depth.
 *
 * Runs after the depth prepass has laid down opaque depth and the resolve has
 * flattened it to a single sample, so the pyramid describes real geometry
 * rather than a cleared buffer.
 *
 * The pyramid is what the GPU occlusion cull tests against, and it is the only
 * thing this pass produces - nothing is drawn to the screen. It follows that
 * the pass runs only while occlusion culling is on; the chain's storage stays
 * allocated across a toggle rather than thrashing on it.
 */
class GLHiZPass : public GLPass {
    public:
        GLHiZPass();
        ~GLHiZPass() override;

        GLHiZPass(const GLHiZPass& other) = delete;
        GLHiZPass& operator=(const GLHiZPass& other) = delete;

        GLHiZPass(GLHiZPass&& other) = delete;
        GLHiZPass& operator=(GLHiZPass&& other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_reduce;
};

} // namespace Engine
