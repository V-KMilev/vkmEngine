#pragma once

#include <memory>

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Screen-space contact shadows for the sun.
 *
 * A fullscreen pass (like GTAO) that reads the resolved depth and marches a short
 * ray toward the sun, writing a visibility mask the forward pass multiplies into
 * the directional light. Runs before the forward pass; a no-op when disabled or
 * when there is no sun.
 */
class GLContactShadowPass : public GLPass {
    public:
        GLContactShadowPass();
        ~GLContactShadowPass() override;

        GLContactShadowPass(const GLContactShadowPass& other) = delete;
        GLContactShadowPass& operator=(const GLContactShadowPass& other) = delete;

        GLContactShadowPass(GLContactShadowPass && other) = delete;
        GLContactShadowPass& operator=(GLContactShadowPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
};

} // namespace Engine
