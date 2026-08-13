#pragma once

#include <memory>

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Depth of field: a circle-of-confusion disk blur over the resolved scene.
 *
 * Fullscreen, using the same scratch ping-pong as fog-apply. Driven by the
 * active camera's focus distance + DoF amount; a no-op when the amount is zero.
 */
class GLDoFPass : public GLPass {
    public:
        GLDoFPass();
        ~GLDoFPass() override;

        GLDoFPass(const GLDoFPass& other) = delete;
        GLDoFPass& operator=(const GLDoFPass& other) = delete;

        GLDoFPass(GLDoFPass && other) = delete;
        GLDoFPass& operator=(GLDoFPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
};

} // namespace Engine
