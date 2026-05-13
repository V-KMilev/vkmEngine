#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>

#include <glm/glm.hpp>

namespace Core {
    class VertexArray;
    class VertexBuffer;
}

namespace Engine {

/**
 * @brief Single GPU buffer of per-instance mat4 model matrices.
 *
 * Backed by a Core::VertexBuffer with stable GL name across orphan resizes —
 * VAO attribute bindings remain valid after the storage grows. Tracks which
 * VAOs have been attached so attachToVAO becomes a no-op after the first call
 * per VAO, eliminating per-frame attribute pointer re-setup.
 *
 * Matrices occupy 4 consecutive vec4 attribute slots (locations startIndex..
 * startIndex+3) with divisor=1 for per-instance data.
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
        /// Upload @p count matrices. Orphan-grows the GL buffer when capacity is
        /// exceeded; the buffer name stays the same, so previously-attached VAO
        /// bindings remain valid.
        void update(const glm::mat4* data, uint32_t count);

        /// Set up @p startIndex .. startIndex+3 as 4 per-instance vec4 attributes on
        /// the given VAO. No-ops on subsequent calls for the same VAO.
        void attachToVAO(Core::VertexArray& vao, uint32_t startIndex = 4);

        uint32_t getInstanceCount() const { return m_instanceCount; }
        uint32_t getCapacity()      const { return m_capacity; }

    private:
        std::unique_ptr<Core::VertexBuffer> m_buffer;
        uint32_t m_capacity     = 0;  ///< Number of mat4s the storage can hold
        uint32_t m_instanceCount = 0;
        std::unordered_set<uint32_t> m_attachedVAOs;

        static constexpr float    GROWTH_FACTOR = 1.5f;
        static constexpr uint32_t MIN_CAPACITY  = 64;
};

} // namespace Engine
