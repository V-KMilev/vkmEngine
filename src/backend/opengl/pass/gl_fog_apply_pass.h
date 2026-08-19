#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {

/**
 * @brief Applies the integrated froxel fog to the resolved scene.
 *
 * A fullscreen pass (the shared scratch ping-pong): samples the scene + depth,
 * looks up the fog volume at the fragment's froxel, and composites
 * scene*transmittance + in-scattering back into the HDR target. A no-op when fog
 * is disabled.
 */
class GLFogApplyPass : public GLPass {
    public:
        GLFogApplyPass();
        ~GLFogApplyPass() override;

        GLFogApplyPass(const GLFogApplyPass& other) = delete;
        GLFogApplyPass& operator=(const GLFogApplyPass& other) = delete;

        GLFogApplyPass(GLFogApplyPass && other) = delete;
        GLFogApplyPass& operator=(GLFogApplyPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;
};

} // namespace Vkm::Engine
