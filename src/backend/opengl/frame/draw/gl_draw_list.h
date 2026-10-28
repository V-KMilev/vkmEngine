#pragma once

#include <cstdint>
#include <vector>

#include "gl_stream_ring.h"

#include "asset/gl_mesh.h"

namespace Vkm::Engine {

/**
 * @brief The two lists a multi-draw reads: object indices, and the commands that slice them.
 *
 * A GLMesh::command per run of instances sharing a mesh, its baseInstance where the run starts.
 * draw() submits commands of one vertex layout as one glMultiDrawElementsIndirect: a draw costs
 * the validation after a state change, paid once per multi-draw, not per mesh. Both lists go into
 * the frame's stream ring, the commands bound to the indirect target.
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
         * @brief Write both lists into @p stream.
         *
         * Once per fill, before the first draw(), in the frame it draws in.
         *
         * @param stream The frame's streaming memory (GLView::stream).
         */
        void upload(Vkm::GL::StreamRing& stream);

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
        const std::vector<uint32_t>& instances() const { return m_instances; }

        /// The instances where upload() wrote them; empty before the first.
        const Vkm::GL::BufferSpan& instanceSpan() const { return m_instanceSpan; }

        /// One command per run of instances sharing a mesh.
        std::vector<DrawCommand>& commands() { return m_commands; }
        const std::vector<DrawCommand>& commands() const { return m_commands; }

    private:
        std::vector<uint32_t>    m_instances;
        std::vector<DrawCommand> m_commands;

        Vkm::GL::BufferSpan m_instanceSpan;
        Vkm::GL::BufferSpan m_commandSpan;
};

} // namespace Vkm::Engine
