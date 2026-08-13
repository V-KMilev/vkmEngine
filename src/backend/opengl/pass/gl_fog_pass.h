#pragma once

#include <memory>

#include "gl_pass.h"

namespace Core {
    class ComputeShader;
}

namespace Engine {

/**
 * @brief Froxel volumetric fog compute: injection + integration.
 *
 * Runs after the cluster cull (it scatters each froxel's cluster lights) and
 * before the fog-apply pass. Injects in-scattered light + extinction into the
 * scatter volume, then marches each column front-to-back into the integrated
 * volume the apply pass samples. A no-op when fog is disabled.
 */
class GLFogPass : public GLPass {
    public:
        GLFogPass();
        ~GLFogPass() override;

        GLFogPass(const GLFogPass& other) = delete;
        GLFogPass& operator=(const GLFogPass& other) = delete;

        GLFogPass(GLFogPass && other) = delete;
        GLFogPass& operator=(GLFogPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::ComputeShader> m_inject;
        std::unique_ptr<Core::ComputeShader> m_integrate;
};

} // namespace Engine
