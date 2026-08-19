#pragma once

#include <GL/glew.h>

#include "gl_vertex_array.h"

namespace Vkm::GL {

/**
 * @brief Attribute-less full-screen triangle.
 *
 * Wraps the empty VAO + 3-vertex draw that every post-processing and
 * fullscreen pass uses. The bound vertex shader is expected to synthesize
 * clip-space positions (and UVs) from gl_VertexID - there is no vertex
 * buffer and no attribute state, so one shared instance covers all passes.
 *
 * Two call shapes are supported:
 *  - draw()                       : one-shot (bind + emit + unbind).
 *  - bind(); emit(); ...; unbind(): many emits under a single VAO binding,
 *                                   for passes that loop over FBO mips /
 *                                   shaders while keeping the VAO bound.
 *
 * Lives in the backend rather than in vkmGL: a fullscreen-triangle draw is a
 * rendering idiom built on GL primitives, not a GL primitive itself.
 */
class ScreenTriangle {
    public:
        ScreenTriangle()  = default;
        ~ScreenTriangle() = default;

        ScreenTriangle(const ScreenTriangle& other) = delete;
        ScreenTriangle& operator=(const ScreenTriangle& other) = delete;

        ScreenTriangle(ScreenTriangle && other) = delete;
        ScreenTriangle& operator=(ScreenTriangle && other) = delete;

    public:
        /// Bind the empty VAO for subsequent emit() calls.
        void bind() const { m_vao.bind(); }

        /// Unbind the empty VAO.
        void unbind() const { m_vao.unbind(); }

        /// Issue the 3-vertex fullscreen draw. The VAO must already be bound
        /// (call bind() first, or use draw() for the one-shot case).
        void emit() const { glDrawArrays(GL_TRIANGLES, 0, 3); }

        /// One-shot fullscreen triangle: bind + emit + unbind.
        void draw() const {
            m_vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 3);
            m_vao.unbind();
        }

    private:
        VertexArray m_vao;
};

} // namespace Vkm::GL
