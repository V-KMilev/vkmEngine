#include "gl_instance_buffer.h"

#include <GL/glew.h>

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_vertex_buffer_layout.h"

namespace Engine {

GLInstanceBuffer::GLInstanceBuffer() : m_buffer(nullptr), m_capacity(0), m_instanceCount(0) {}

GLInstanceBuffer::~GLInstanceBuffer() {
    m_buffer.reset();
}

void GLInstanceBuffer::update(const glm::mat4* data, uint32_t count) {
    m_instanceCount = count;

    if (count == 0) {
        return;
    }

    const uint32_t dataSize = count * sizeof(glm::mat4);

    if (!m_buffer || count > m_capacity) {
        // Need to allocate or grow the buffer
        uint32_t newCapacity = std::max(MIN_CAPACITY, count);
        if (m_buffer && count > m_capacity) {
            // Grow by factor
            newCapacity = std::max(newCapacity, static_cast<uint32_t>(m_capacity * GROWTH_FACTOR));
        }

        const uint32_t bufferSize = newCapacity * sizeof(glm::mat4);
        // Allocate buffer with capacity size, but only upload actual data
        m_buffer = std::make_unique<Core::VertexBuffer>(nullptr, bufferSize, GL_DYNAMIC_DRAW);
        m_buffer->update(data, dataSize, 0);
        m_capacity = newCapacity;
    } else {
        // Update existing buffer with new data
        m_buffer->update(data, dataSize, 0);
    }
}

void GLInstanceBuffer::attachToVAO(Core::VertexArray& vao, uint32_t startIndex) {
    if (!m_buffer) {
        return;
    }

    vao.bind();
    m_buffer->bind();

    // A mat4 is 4 vec4s, each needs its own attribute slot
    // We set up 4 consecutive vec4 attributes for the model matrix columns
    const uint32_t vec4Size = sizeof(glm::vec4);
    const uint32_t mat4Stride = sizeof(glm::mat4);

    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t attribIndex = startIndex + i;

        glEnableVertexAttribArray(attribIndex);
        glVertexAttribPointer(
            attribIndex,
            4,                              // vec4
            GL_FLOAT,
            GL_FALSE,
            mat4Stride,                     // stride = size of entire mat4
            reinterpret_cast<const void*>(i * vec4Size)  // offset to this column
        );
        // Set divisor = 1 so attribute advances once per instance
        vao.setAttributeDivisor(attribIndex, 1);
    }
}

} // namespace Engine
