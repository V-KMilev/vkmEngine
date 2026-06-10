#pragma once

#include <memory>

#include "gl_screen_triangle.h"  // Core::ScreenTriangle (by-value member)

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Resolves the HDR scene target to the backbuffer.
 *
 * Runs last: binds the default framebuffer, samples the frame's HDR target, and
 * tonemaps + gamma-corrects it across a fullscreen triangle. This is where the
 * pipeline goes from linear HDR back to a displayable image.
 */
class GLCompositePass : public GLPass {
    public:
        GLCompositePass();
        ~GLCompositePass() override;

        GLCompositePass(const GLCompositePass& other) = delete;
        GLCompositePass& operator=(const GLCompositePass& other) = delete;

        GLCompositePass(GLCompositePass && other) = delete;
        GLCompositePass& operator=(GLCompositePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
        Core::ScreenTriangle          m_triangle;
};

} // namespace Engine
