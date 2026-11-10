#pragma once

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws the frame's DebugDraw lines (RenderView::SceneLists::debug).
 *
 * Each line is a quad round its segment, its pixels measured against the segment so it is
 * smooth-edged and round-ended at its width in pixels; drawn where the scene's depth does not hide
 * it, and where it does as its DebugDepth says. After Grid, over the tonemapped frame in display
 * colors; it does not draw without lines and a camera.
 */
class GLDebugDrawPass : public GLPass {
    public:
        GLDebugDrawPass();
        ~GLDebugDrawPass() override;

        GLDebugDrawPass(const GLDebugDrawPass& other) = delete;
        GLDebugDrawPass& operator=(const GLDebugDrawPass& other) = delete;

        GLDebugDrawPass(GLDebugDrawPass && other) = delete;
        GLDebugDrawPass& operator=(GLDebugDrawPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::Shader m_shader;
};

} // namespace Vkm::Engine
