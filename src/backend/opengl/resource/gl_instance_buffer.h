#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace Core {
    class VertexArray;
    class VertexBuffer;
}

namespace Engine {

/**
 * @brief Manages a GPU buffer for instance matrix data.
 *
 * Provides efficient per-frame updates of model matrices for instanced rendering.
 * Uses orphan pattern for buffer updates to avoid pipeline stalls.
 *
 * The buffer stores mat4 model matrices, which occupy 4 consecutive vec4
 * vertex attributes (locations 4-7) with divisor=1 for per-instance data.
 */
class GLInstanceBuffer {
    public:
        GLInstanceBuffer();
        ~GLInstanceBuffer();

        GLInstanceBuffer(const GLInstanceBuffer& other) = delete;
        GLInstanceBuffer& operator=(const GLInstanceBuffer& other) = delete;
        GLInstanceBuffer(GLInstanceBuffer && other) = delete;
        GLInstanceBuffer& operator=(GLInstanceBuffer && other) = delete;

    public:
        /**
        * @brief Updates the buffer with new instance matrices.
        *
        * Uses orphan pattern: if capacity is sufficient, orphans and refills.
        * If capacity is insufficient, reallocates with growth factor.
        *
        * @param data Pointer to contiguous model matrices.
        * @param count Number of instances.
        */
        void update(const glm::mat4* data, uint32_t count);

        /**
        * @brief Attaches the instance buffer to a VAO at the specified attribute locations.
        *
        * Sets up 4 vec4 attributes (for mat4) at locations startIndex through startIndex+3,
        * with attribute divisor = 1 for per-instance data.
        *
        * @param vao The VAO to attach to
        * @param startIndex Starting attribute location (default: 4)
        */
        void attachToVAO(Core::VertexArray& vao, uint32_t startIndex = 4);

        /**
        * @brief Returns current instance count.
        */
        uint32_t getInstanceCount() const { return m_instanceCount; }

        /**
        * @brief Returns current buffer capacity in number of instances.
        */
        uint32_t getCapacity() const { return m_capacity; }

    private:
        std::unique_ptr<Core::VertexBuffer> m_buffer;
        uint32_t m_capacity;
        uint32_t m_instanceCount;

        static constexpr float GROWTH_FACTOR = 1.5f;
        static constexpr uint32_t MIN_CAPACITY = 64;
};

} // namespace Engine
