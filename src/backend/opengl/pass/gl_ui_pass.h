#pragma once

#include <cstdint>
#include <memory>

#include "gl_pass.h"

namespace Core {
    class Shader;
    class VertexArray;
    class VertexBuffer;
}

namespace Engine {

/**
 * @brief Draws the UI overlay on top of the composited scene.
 *
 * The last pass: once Composite has resolved the scene to the backbuffer, this
 * streams the frame's UIDrawData (built by the UISystem, carried in the
 * RenderView) into a dynamic vertex buffer and draws it under an orthographic
 * projection over the viewport rect. Alpha-blended with depth off - a flat 2D
 * layer. A no-op when the draw list is empty.
 */
class GLUIPass : public GLPass {
    public:
        GLUIPass();
        ~GLUIPass() override;

        GLUIPass(const GLUIPass& other) = delete;
        GLUIPass& operator=(const GLUIPass& other) = delete;

        GLUIPass(GLUIPass && other) = delete;
        GLUIPass& operator=(GLUIPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Grow the dynamic vertex buffer to hold at least @p vertexCount
         * vertices, (re)binding it to the VAO when it reallocates.
         */
        void ensureCapacity(uint32_t vertexCount);

    private:
        std::unique_ptr<Core::Shader>       m_shader;
        std::unique_ptr<Core::VertexArray>  m_vao;
        std::unique_ptr<Core::VertexBuffer> m_vbo;
        uint32_t                            m_capacity = 0;  ///< VBO capacity in vertices.
};

} // namespace Engine
