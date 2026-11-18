#pragma once

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"
#include "storage/gl_target.h"

namespace Vkm::Engine {

/**
 * @brief A scene rendered smaller than the viewport, widened into the window by an edge-adaptive
 *        upscale and a contrast-adaptive sharpen (Lottes 2021).
 *
 * Runs when the composite left its frame at the scene's size (GLFrameContext::upscaleSource): EASU upscales
 * it edge-adaptively into a viewport-size target, then RCAS sharpens that into the window and dithers it
 * there (shaders/opengl/post/upscale, under its MIT notice). After the tonemap, as the upscale wants, and
 * before what draws over the frame - the grid, the debug lines, the UI - which stays at the window's
 * resolution.
 */
class GLUpscalePass : public GLPass {
    public:
        GLUpscalePass();
        ~GLUpscalePass() override;

        GLUpscalePass(const GLUpscalePass& other) = delete;
        GLUpscalePass& operator=(const GLUpscalePass& other) = delete;

        GLUpscalePass(GLUpscalePass && other) = delete;
        GLUpscalePass& operator=(GLUpscalePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::Shader m_easu;
        Vkm::OpenGL::Shader m_rcas;
        GLTarget            m_scaled{GLTarget::Layout::Color, GL_RGB10_A2};
            ///< EASU's output at the viewport's size, display-encoded: ten bits keep RCAS's input unbanded.
};

} // namespace Vkm::Engine
