#include "gl_instance_buffer.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_error_handle.h"

namespace Engine {

GLInstanceBuffer::GLInstanceBuffer() = default;
GLInstanceBuffer::~GLInstanceBuffer() = default;

void GLInstanceBuffer::update(const glm::mat4* data, uint32_t count) {
    m_instanceCount = count;
    if (count == 0) return;

    const uint32_t dataSize = count * sizeof(glm::mat4);

    if (!m_buffer) {
        m_capacity = std::max(MIN_CAPACITY, count);
        m_buffer = std::make_unique<Core::VertexBuffer>(
            nullptr, m_capacity * sizeof(glm::mat4), GL_STREAM_DRAW);
        m_buffer->update(data, dataSize, 0);
    } else if (count > m_capacity) {
        // Orphan-grow on the existing buffer: same GL name (so VAO attribute
        // bindings remain valid), new storage. Bind via wrapper to keep the
        // shared bind cache consistent, then resize directly.
        m_capacity = std::max(static_cast<uint32_t>(m_capacity * GROWTH_FACTOR), count);
        m_buffer->bind();
        VKM_GL_CHECK(glBufferData(
            GL_ARRAY_BUFFER, m_capacity * sizeof(glm::mat4), nullptr, GL_STREAM_DRAW));
        m_buffer->update(data, dataSize, 0);
    } else {
        m_buffer->update(data, dataSize, 0);
    }
}

void GLInstanceBuffer::attachToVAO(Core::VertexArray& vao, uint32_t startIndex) {
    if (!m_buffer) return;

    const uint32_t vaoId = vao.getID();
    if (!m_attachedVAOs.insert(vaoId).second) return;

    vao.bind();
    m_buffer->bind();

    constexpr uint32_t vec4Size   = sizeof(glm::vec4);
    constexpr uint32_t mat4Stride = sizeof(glm::mat4);

    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t attribIndex = startIndex + i;

        VKM_GL_CHECK(glEnableVertexAttribArray(attribIndex));
        VKM_GL_CHECK(glVertexAttribPointer(
            attribIndex,
            4,
            GL_FLOAT,
            GL_FALSE,
            mat4Stride,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(i * vec4Size))
        ));
        vao.setAttributeDivisor(attribIndex, 1);
    }
}

} // namespace Engine
