#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gl_vertex_buffer.h"

#include "asset/gl_mesh.h"

namespace Vkm::Engine {

/**
 * @brief The two lists a multi-draw reads: object indices, and the commands that slice them.
 *
 * A GLMesh::command per run of instances sharing a mesh, its baseInstance where the run starts.
 * draw() submits commands of one vertex layout as one glMultiDrawElementsIndirect: on this
 * driver a draw costs the validation after a state change, paid once per multi-draw, not per
 * mesh. The command buffer is a vertex buffer bound to the indirect target; vkmGL has no
 * indirect one.
 */
class GLDrawList {
    public:
        GLDrawList();
        ~GLDrawList();

        GLDrawList(const GLDrawList& other) = delete;
        GLDrawList& operator=(const GLDrawList& other) = delete;

        GLDrawList(GLDrawList && other) = delete;
        GLDrawList& operator=(GLDrawList && other) = delete;

    public:
        /// Empty both lists, keeping their capacity.
        void clear();

        /**
         * @brief Send both lists to the GPU, growing their buffers when needed.
         *
         * Once per fill, before the first draw().
         */
        void upload();

        /**
         * @brief Submit @p count commands from @p first as one multi-draw.
         *
         * @param mesh  Any of the commands' meshes; all share its pool and vertex layout.
         * @param first First command.
         * @param count Commands to draw.
         */
        void draw(const GLMesh& mesh, uint32_t first, uint32_t count) const;

        /// One object index per instance, in the order the commands read them.
        std::vector<uint32_t>& instances() { return m_instances; }

        /// One command per run of instances sharing a mesh.
        std::vector<DrawCommand>& commands() { return m_commands; }

    private:
        std::vector<uint32_t>    m_instances;
        std::vector<DrawCommand> m_commands;

        std::unique_ptr<Vkm::GL::VertexBuffer> m_instanceBuffer;
        std::unique_ptr<Vkm::GL::VertexBuffer> m_commandBuffer;
        uint32_t m_instanceCapacity = 0;  ///< Bytes m_instanceBuffer holds.
        uint32_t m_commandCapacity  = 0;  ///< Bytes m_commandBuffer holds.
};

} // namespace Vkm::Engine
