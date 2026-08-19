#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class ComputeShader;
}

namespace Engine {

/**
 * @brief Forward+ light cull: dispatches the cluster-cull compute shader that
 * fills the per-cluster light lists the forward pass reads.
 *
 * Runs before the forward pass. Reads the light SSBO (bound by the backend),
 * then issues a shader-storage barrier so the forward pass sees the writes.
 */
class GLClusterPass : public GLPass {
    public:
        GLClusterPass();
        ~GLClusterPass() override;

        GLClusterPass(const GLClusterPass& other) = delete;
        GLClusterPass& operator=(const GLClusterPass& other) = delete;

        GLClusterPass(GLClusterPass && other) = delete;
        GLClusterPass& operator=(GLClusterPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::ComputeShader> m_compute;
};

} // namespace Engine
