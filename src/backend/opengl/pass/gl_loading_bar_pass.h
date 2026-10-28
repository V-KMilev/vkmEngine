#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief The loading screen's bar (loadingBar), over its backdrop and a project's own screen, while
 *        it shows and no splash does.
 *
 * A line of water running along the part filled (shaders/loading/bar), eased toward the progress
 * (RenderView::loadingProgress), coming in as the status line does (RenderView::loadingContent).
 */
class GLLoadingBarPass : public GLPass {
    public:
        GLLoadingBarPass();
        ~GLLoadingBarPass() override;

        GLLoadingBarPass(const GLLoadingBarPass& other) = delete;
        GLLoadingBarPass& operator=(const GLLoadingBarPass& other) = delete;

        GLLoadingBarPass(GLLoadingBarPass && other) = delete;
        GLLoadingBarPass& operator=(GLLoadingBarPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader m_bar;  ///< shaders/loading/bar

        float m_fill  = 0.0f;   ///< How far it shows filled, easing toward the progress.
        float m_time  = 0.0f;   ///< Seconds since the screen came up.
        bool  m_shown = false;  ///< Whether the last frame drew it.
};

} // namespace Vkm::Engine
