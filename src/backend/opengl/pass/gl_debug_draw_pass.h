#pragma once

#include <cstdint>

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws the frame's DebugDraw lines (RenderView::debug), in one of two stages.
 *
 * Each line is a quad round its segment, its pixels measured against the segment so it is
 * smooth-edged and round-ended at its width in pixels; drawn where the scene's depth does not hide
 * it, and where it does as its DebugDepth says. Glow, before Bloom, adds the light of the lines
 * that glow into the scene's color where they are seen, for the bloom to spread; Lines, after
 * Grid, draws them all over the tonemapped frame in display colors. The first stage to draw
 * uploads the lines once for both (GLFrameContext::debugLines); neither draws without lines and a
 * camera.
 */
class GLDebugDrawPass : public GLPass {
    public:
        enum class Stage : uint8_t {
            Glow,
            Lines,
        };

        explicit GLDebugDrawPass(Stage stage);
        ~GLDebugDrawPass() override;

        GLDebugDrawPass(const GLDebugDrawPass& other) = delete;
        GLDebugDrawPass& operator=(const GLDebugDrawPass& other) = delete;

        GLDebugDrawPass(GLDebugDrawPass && other) = delete;
        GLDebugDrawPass& operator=(GLDebugDrawPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Stage           m_stage;
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
