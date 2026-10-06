#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gl_vertex_array.h"

namespace Vkm::GL {
    class VertexBuffer;
}

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Which vertex streams a mesh carries, and so which of the pool's vertex arrays draws it.
 */
enum class VertexLayout : uint8_t {
    Static,   ///< Vertex alone.
    Skinned,  ///< Vertex, and its SkinVertex in a parallel stream.
    Count,
};

/**
 * @brief One record of glMultiDrawElementsIndirect's command list, laid out as GL reads it.
 *
 * baseInstance offsets every attribute at a non-zero divisor, so each command reads its own
 * slice of one object-index list.
 */
struct DrawCommand {
    uint32_t count         = 0;  ///< Indices per instance.
    uint32_t instanceCount = 0;
    uint32_t firstIndex    = 0;  ///< Into the pool's index buffer.
    int32_t  baseVertex    = 0;  ///< Added to every index: where the mesh's vertices start.
    uint32_t baseInstance  = 0;  ///< First entry of the instance list the command reads.
};
static_assert(sizeof(DrawCommand) == 20, "GL reads the indirect command as five packed words");

/**
 * @brief Where one mesh's vertices and indices sit in a GLMeshPool.
 */
struct MeshRange {
    VertexLayout layout      = VertexLayout::Static;
    uint32_t     firstVertex = 0;
    uint32_t     vertexCount = 0;
    uint32_t     firstIndex  = 0;
    uint32_t     indexCount  = 0;
};

/**
 * @brief A free list over a buffer measured in elements: first fit, merged on release.
 *
 * Meshes come and go with assets, not frames, so the list stays short and cheap to walk.
 */
struct SpanList {
    /// One free run of elements.
    struct Span {
        uint32_t offset = 0;
        uint32_t size   = 0;
    };

    std::vector<Span> free;          ///< Free runs, by offset, never adjacent.
    uint32_t          capacity = 0;  ///< Elements the buffer holds.
};

/**
 * @brief Every mesh's vertices and indices, in one buffer per stream.
 *
 * A multi-draw takes many meshes from one vertex array and index buffer, each command finding
 * its mesh by firstIndex and baseVertex: so one index buffer, and one vertex array per
 * VertexLayout.
 *
 * The skin stream sits at divisor 0, so locations 8/9 stay enabled while a program that never
 * declares them draws a skinned mesh in bind pose. Location 4, the object index, is at divisor
 * 1 on both arrays; a draw with no list reads a zero the pool owns, since drawing with an
 * enabled attribute that has no buffer is an error. A full stream doubles, copying on the GPU;
 * every GLMesh keeps its range, since offsets do not move.
 */
class GLMeshPool {
    public:
        GLMeshPool();
        ~GLMeshPool();

        GLMeshPool(const GLMeshPool& other) = delete;
        GLMeshPool& operator=(const GLMeshPool& other) = delete;

        GLMeshPool(GLMeshPool && other) = delete;
        GLMeshPool& operator=(GLMeshPool && other) = delete;

    public:
        /**
         * @brief Upload @p mesh into free space, growing a stream that has none.
         *
         * @param mesh The geometry; a non-empty `skin` puts it in the skinned layout.
         * @return Where it went, for remove() and the draw commands; empty for no geometry, a
         *         skin not one entry per vertex, or no room.
         */
        MeshRange add(const MeshAsset& mesh);

        /**
         * @brief Give @p range's space back.
         *
         * A draw already submitted keeps the contents it was submitted with: GL
         * orders the next upload into the space after it.
         *
         * @param range What add() returned.
         */
        void remove(const MeshRange& range);

        /**
         * @brief Draw one instance of @p range with no instance list.
         *
         * For a program that reads no object index.
         *
         * @param range The mesh to draw.
         */
        void draw(const MeshRange& range) const;

        /**
         * @brief Submit @p count of @p commands from @p first as one multi-draw over @p layout.
         *
         * @param layout    The vertex array every command's mesh lives in.
         * @param instances The object indices the commands' baseInstance address.
         * @param commands  DrawCommand records, bound as the indirect buffer.
         * @param first     First command to draw.
         * @param count     Commands to draw.
         */
        void drawIndirect(
            VertexLayout layout,
            const Vkm::GL::VertexBuffer& instances,
            const Vkm::GL::VertexBuffer& commands,
            uint32_t first,
            uint32_t count
        ) const;

    private:
        /**
         * @brief Bind @p layout's vertex array and index buffer, @p instances behind the object index.
         *
         * The index buffer is bound every time although the array records it: any element buffer
         * created while an array is bound replaces its binding.
         *
         * @param layout    Which vertex array: static, or skinned with its second stream.
         * @param instances The object indices the draw reads, one per instance.
         */
        void bind(VertexLayout layout, const Vkm::GL::VertexBuffer& instances) const;

        /**
         * @brief Point the vertex arrays at the current buffers.
         *
         * Called by add() after reserving: a stream that grew has a new buffer object.
         */
        void wire();

    private:
        Vkm::GL::VertexArray m_arrays[static_cast<size_t>(VertexLayout::Count)];

        std::unique_ptr<Vkm::GL::VertexBuffer> m_indices;
        std::unique_ptr<Vkm::GL::VertexBuffer> m_vertices[static_cast<size_t>(VertexLayout::Count)];
        std::unique_ptr<Vkm::GL::VertexBuffer> m_skin;      ///< Parallel to m_vertices[Skinned].
        /// One zero, behind location 4 for a draw with no list.
        std::unique_ptr<Vkm::GL::VertexBuffer> m_noObject;

        SpanList m_indexSpans;
        SpanList m_vertexSpans[static_cast<size_t>(VertexLayout::Count)];
};

} // namespace Vkm::Engine
