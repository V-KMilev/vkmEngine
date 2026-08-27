#pragma once

#include <cstdint>
#include <memory>
#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_error_handle.h"

namespace Vkm::GL {

/**
 * @brief GPU buffer of per-instance mat4 model matrices.
 *
 * Lives in the backend rather than in vkmGL: per-instance model matrices are a
 * renderer concept, and glm has no business in a GL wrapper's API. Backed by a
 * Vkm::GL::VertexBuffer with a stable GL name across orphan resizes, so VAO
 * attribute bindings stay valid after the storage grows. The matrix occupies
 * 4 consecutive vec4 attribute slots (locations startIndex..startIndex+3)
 * with divisor = 1.
 *
 * Pure mechanism: attachToVAO always (re)installs the attribute pointers. It
 * keeps no attach cache and no cross-instance state - deciding when a VAO
 * needs re-binding (e.g. when several instance buffers share one VAO) is a
 * consumer-side policy, deliberately kept out of this primitive.
 */
class InstanceBuffer {
    public:
        InstanceBuffer() = default;
        ~InstanceBuffer() = default;

        InstanceBuffer(const InstanceBuffer& other) = delete;
        InstanceBuffer& operator=(const InstanceBuffer& other) = delete;

        InstanceBuffer(InstanceBuffer && other) = delete;
        InstanceBuffer& operator=(InstanceBuffer && other) = delete;

        /**
         * @brief Upload @p count matrices, growing the buffer when they do not fit.
         *
         * The grow orphans the storage rather than replacing the buffer, so the
         * GL name stays the same and VAO bindings attached earlier remain valid.
         *
         * @param data  Matrices to upload, tightly packed; read only for @p count.
         * @param count How many matrices; zero uploads nothing.
         */
        void update(const glm::mat4* data, uint32_t count) {
            m_instanceCount = count;
            if (count == 0) return;

            const uint32_t dataSize = count * sizeof(glm::mat4);

            if (!m_buffer) {
                m_capacity = std::max(MIN_CAPACITY, count);
                m_buffer = std::make_unique<Vkm::GL::VertexBuffer>(
                    nullptr, m_capacity * sizeof(glm::mat4), GL_STREAM_DRAW);
                m_buffer->update(data, dataSize, 0);
            } else if (count > m_capacity) {
                m_capacity = std::max(
                    static_cast<uint32_t>(m_capacity * GROWTH_FACTOR), count);
                m_buffer->allocate(m_capacity * sizeof(glm::mat4));
                m_buffer->update(data, dataSize, 0);
            } else {
                m_buffer->update(data, dataSize, 0);
            }
        }

        /**
         * @brief Install the matrix as four per-instance vec4 attributes.
         *
         * The setup runs every call - nothing is cached - so a VAO rebuilt for
         * any other reason picks the bindings up again.
         *
         * @param vao        Vertex array the attributes are installed on.
         * @param startIndex First of the four consecutive attribute slots used.
         */
        void attachToVAO(Vkm::GL::VertexArray& vao, uint32_t startIndex = 4) {
            if (!m_buffer) return;

            vao.bind();
            m_buffer->bind();

            constexpr uint32_t vec4Size   = sizeof(glm::vec4);
            constexpr uint32_t mat4Stride = sizeof(glm::mat4);

            for (uint32_t i = 0; i < 4; ++i) {
                const uint32_t attribIndex = startIndex + i;
                VKM_GL_CHECK(glEnableVertexAttribArray(attribIndex));
                VKM_GL_CHECK(glVertexAttribPointer(
                    attribIndex, 4, GL_FLOAT, GL_FALSE, mat4Stride,
                    reinterpret_cast<const void*>(
                        static_cast<uintptr_t>(i * vec4Size))));
                vao.setAttributeDivisor(attribIndex, 1);
            }
        }

        /// GL name, for binding the same storage as an SSBO a compute stage reads or writes.
        uint32_t id() const { return m_buffer ? m_buffer->getID() : 0; }

        uint32_t getInstanceCount() const { return m_instanceCount; }
        uint32_t getCapacity()      const { return m_capacity; }

    private:
        static constexpr float    GROWTH_FACTOR = 1.5f;
        static constexpr uint32_t MIN_CAPACITY  = 64;

    private:
        std::unique_ptr<Vkm::GL::VertexBuffer> m_buffer;
        uint32_t m_capacity      = 0;
        uint32_t m_instanceCount = 0;
};

} // namespace Vkm::GL
