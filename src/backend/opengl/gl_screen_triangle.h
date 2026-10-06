#pragma once

#include <GL/glew.h>

#include "gl_vertex_array.h"

namespace Vkm::Engine {

/**
 * @brief Attribute-less full-screen triangle.
 *
 * Wraps the empty VAO + 3-vertex draw a fullscreen pass uses. The bound
 * vertex shader is expected to synthesize clip-space positions (and UVs) from
 * gl_VertexID - there is no vertex buffer and no attribute state, so one
 * shared instance covers all passes.
 *
 * Two call shapes are supported:
 *  - draw()                       : one-shot (bind + emit + unbind).
 *  - bind(); emit(); ...; unbind(): many emits under a single VAO binding,
 *                                   for passes that loop over FBO mips /
 *                                   shaders while keeping the VAO bound.
 *
 * bind() alone lends the empty VAO to any other attribute-less draw, since
 * core profile needs one bound and one is enough.
 *
 * A rendering idiom built on GL primitives rather than a primitive itself, so
 * it is the backend's, not vkmGL's.
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

        void unbind() const { m_vao.unbind(); }

        /**
         * @brief Issue the 3-vertex fullscreen draw.
         *
         * The VAO must already be bound: call bind() first, or draw() for the
         * one-shot case.
         */
        void emit() const { m_vao.drawArrays(GL_TRIANGLES, 0, 3); }

        /// One-shot fullscreen triangle: bind + emit + unbind.
        void draw() const {
            m_vao.bind();
            m_vao.drawArrays(GL_TRIANGLES, 0, 3);
            m_vao.unbind();
        }

    private:
        Vkm::GL::VertexArray m_vao;
};

} // namespace Vkm::Engine
