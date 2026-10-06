#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Depth of field: a circle-of-confusion disk blur over the resolved scene.
 *
 * Fullscreen, through the colour chain's ping-pong; a no-op when the camera's DoF amount is zero.
 */
class GLDoFPass : public GLPass {
    public:
        GLDoFPass();
        ~GLDoFPass() override;

        GLDoFPass(const GLDoFPass& other) = delete;
        GLDoFPass& operator=(const GLDoFPass& other) = delete;

        GLDoFPass(GLDoFPass && other) = delete;
        GLDoFPass& operator=(GLDoFPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
