#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "asset/gl_mesh_pool.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include <GL/glew.h>

#include "logger.h"

#include "gl_error_handle.h"
#include "gl_vertex_buffer.h"

#include "convention/gl_bindings.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

namespace {

namespace Attr = GLBindings::VertexAttributes;

// Vertex buffer binding points, a namespace apart from the locations.
constexpr uint32_t VERTEX_BINDING   = 0;
constexpr uint32_t SKIN_BINDING     = 1;
constexpr uint32_t INSTANCE_BINDING = 4;

// What a stream starts at the first time anything is put in it, in elements:
// a few hundred props' worth, so a small scene never grows.
constexpr uint32_t MIN_VERTICES = 1u << 16;
constexpr uint32_t MIN_INDICES  = 1u << 18;

constexpr size_t SKINNED = static_cast<size_t>(VertexLayout::Skinned);

std::optional<uint32_t> take(SpanList& list, uint32_t count) {
    for (auto span = list.free.begin(); span != list.free.end(); ++span) {
        if (span->size < count) continue;
        const uint32_t offset = span->offset;
        span->offset += count;
        span->size   -= count;
        if (span->size == 0) list.free.erase(span);
        return offset;
    }
    return std::nullopt;
}

void give(SpanList& list, uint32_t offset, uint32_t count) {
    if (count == 0) return;
    auto next = std::lower_bound(
        list.free.begin(),
        list.free.end(),
        offset,
        [](const SpanList::Span& span, uint32_t at) { return span.offset < at; }
    );
    // A run that touches the one before it, the one after it, or both, joins
    // them, so the list never holds two spans a single request could have used.
    if (next != list.free.begin()) {
        SpanList::Span& prev = *(next - 1);
        if (prev.offset + prev.size == offset) {
            prev.size += count;
            if (next != list.free.end() && prev.offset + prev.size == next->offset) {
                prev.size += next->size;
                list.free.erase(next);
            }
            return;
        }
    }
    if (next != list.free.end() && offset + count == next->offset) {
        next->offset = offset;
        next->size  += count;
        return;
    }
    list.free.insert(next, {offset, count});
}

// Replace @p buffer with one of @p newBytes holding its first @p oldBytes.
void growBuffer(std::unique_ptr<Vkm::GL::VertexBuffer>& buffer, uint32_t oldBytes, uint32_t newBytes) {
    auto bigger = std::make_unique<Vkm::GL::VertexBuffer>(nullptr, newBytes, GL_STATIC_DRAW);
    if (buffer && oldBytes > 0) {
        VKM_GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, buffer->getID()));
        VKM_GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, bigger->getID()));
        VKM_GL_CHECK(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, oldBytes));
    }
    buffer = std::move(bigger);
}

/**
 * @brief Reserve @p count elements of @p list, growing it first when no span fits.
 *
 * @param list    The stream's free list.
 * @param count   Elements wanted.
 * @param minimum The capacity a stream starts at.
 * @param stride  Bytes per element of the widest buffer the list governs.
 * @param grow    Called with the old and new capacity to grow every buffer the list governs.
 * @return The offset, or nothing when the stream cannot be made large enough.
 */
template <typename Grow>
std::optional<uint32_t> reserve(
    SpanList& list,
    uint32_t count,
    uint32_t minimum,
    uint32_t stride,
    Grow&& grow
) {
    if (const std::optional<uint32_t> at = take(list, count)) return at;

    // A GL buffer here is sized in 32 bits of bytes.
    const uint64_t most     = UINT32_MAX / stride;
    const uint64_t held     = list.capacity;
    const uint64_t wanted   = std::max<uint64_t>({held * 2, held + count, minimum});
    const uint64_t capacity = std::min(wanted, most);
    if (capacity < uint64_t{list.capacity} + count) return std::nullopt;

    grow(list.capacity, static_cast<uint32_t>(capacity));
    give(list, list.capacity, static_cast<uint32_t>(capacity) - list.capacity);
    list.capacity = static_cast<uint32_t>(capacity);
    return take(list, count);
}

} // namespace

GLMeshPool::GLMeshPool() {
    const uint32_t zero = 0;
    m_noObject = std::make_unique<Vkm::GL::VertexBuffer>(&zero, static_cast<uint32_t>(sizeof(zero)));

    for (size_t layout = 0; layout < static_cast<size_t>(VertexLayout::Count); ++layout) {
        m_arrays[layout].bind();

        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::POSITION));
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::NORMAL));
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::UV));
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::TANGENT));
        VKM_GL_CHECK(glVertexAttribFormat(Attr::POSITION, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position)));
        VKM_GL_CHECK(glVertexAttribFormat(Attr::NORMAL,   3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal)));
        VKM_GL_CHECK(glVertexAttribFormat(Attr::UV,       2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv)));
        VKM_GL_CHECK(glVertexAttribFormat(Attr::TANGENT,  4, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent)));
        for (const uint32_t attrib : {Attr::POSITION, Attr::NORMAL, Attr::UV, Attr::TANGENT}) {
            VKM_GL_CHECK(glVertexAttribBinding(attrib, VERTEX_BINDING));
        }

        // The object index: an integer attribute (the float entry point would
        // convert it and lose slots past 2^24), stepping once per instance.
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::INSTANCE));
        VKM_GL_CHECK(glVertexAttribIFormat(Attr::INSTANCE, 1, GL_UNSIGNED_INT, 0));
        VKM_GL_CHECK(glVertexAttribBinding(Attr::INSTANCE, INSTANCE_BINDING));
        VKM_GL_CHECK(glVertexBindingDivisor(INSTANCE_BINDING, 1));
        VKM_GL_CHECK(glBindVertexBuffer(INSTANCE_BINDING, m_noObject->getID(), 0, sizeof(uint32_t)));

        if (layout != SKINNED) continue;
        // Bone indices stay integers; weights arrive as unorm8 summing to 1.0.
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::BONES));
        VKM_GL_CHECK(glEnableVertexAttribArray(Attr::WEIGHTS));
        VKM_GL_CHECK(glVertexAttribIFormat(Attr::BONES, 4, GL_UNSIGNED_SHORT, offsetof(SkinVertex, bones)));
        VKM_GL_CHECK(
            glVertexAttribFormat(Attr::WEIGHTS, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(SkinVertex, weights))
        );
        VKM_GL_CHECK(glVertexAttribBinding(Attr::BONES, SKIN_BINDING));
        VKM_GL_CHECK(glVertexAttribBinding(Attr::WEIGHTS, SKIN_BINDING));
    }
    VKM_GL_CHECK(glBindVertexArray(0));
}

GLMeshPool::~GLMeshPool() = default;

MeshRange GLMeshPool::add(const MeshAsset& mesh) {
    MeshRange range;
    range.layout = mesh.skin.empty() ? VertexLayout::Static : VertexLayout::Skinned;
    const size_t layout = static_cast<size_t>(range.layout);

    const auto vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    const auto indexCount  = static_cast<uint32_t>(mesh.indices.size());
    if (vertexCount == 0 || indexCount == 0) return MeshRange{range.layout};
    // The skin stream is uploaded a vertex's worth per vertex: a shorter one
    // would be read past its end.
    if (!mesh.skin.empty() && mesh.skin.size() != mesh.vertices.size()) {
        LOG_ERROR(
            "A skinned mesh has %zu skin entries for %u vertices; it is not drawn",
            mesh.skin.size(),
            vertexCount
        );
        return MeshRange{range.layout};
    }

    std::unique_ptr<Vkm::GL::VertexBuffer>& vertices = m_vertices[layout];
    const std::optional<uint32_t> firstVertex = reserve(
        m_vertexSpans[layout],
        vertexCount,
        MIN_VERTICES,
        sizeof(Vertex),
        [&](uint32_t from, uint32_t to) {
            growBuffer(vertices, from * sizeof(Vertex), to * sizeof(Vertex));
            if (layout == SKINNED) growBuffer(m_skin, from * sizeof(SkinVertex), to * sizeof(SkinVertex));
        }
    );
    const std::optional<uint32_t> firstIndex = reserve(
        m_indexSpans,
        indexCount,
        MIN_INDICES,
        sizeof(uint32_t),
        [&](uint32_t from, uint32_t to) {
            growBuffer(m_indices, from * sizeof(uint32_t), to * sizeof(uint32_t));
        }
    );
    wire();

    if (!firstVertex || !firstIndex) {
        if (firstVertex) give(m_vertexSpans[layout], *firstVertex, vertexCount);
        if (firstIndex)  give(m_indexSpans, *firstIndex, indexCount);
        LOG_ERROR(
            "A mesh of %u vertices and %u indices does not fit the mesh pool; it is not drawn",
            vertexCount,
            indexCount
        );
        return MeshRange{range.layout};
    }

    range.firstVertex = *firstVertex;
    range.vertexCount = vertexCount;
    range.firstIndex  = *firstIndex;
    range.indexCount  = indexCount;

    vertices->update(mesh.vertices.data(), vertexCount * sizeof(Vertex), range.firstVertex * sizeof(Vertex));
    if (range.layout == VertexLayout::Skinned) {
        m_skin->update(
            mesh.skin.data(),
            vertexCount * sizeof(SkinVertex),
            range.firstVertex * sizeof(SkinVertex)
        );
    }
    m_indices->update(
        mesh.indices.data(),
        indexCount * sizeof(uint32_t),
        range.firstIndex * sizeof(uint32_t)
    );
    return range;
}

void GLMeshPool::remove(const MeshRange& range) {
    give(m_vertexSpans[static_cast<size_t>(range.layout)], range.firstVertex, range.vertexCount);
    give(m_indexSpans, range.firstIndex, range.indexCount);
}

void GLMeshPool::draw(const MeshRange& range) const {
    if (range.indexCount == 0) return;
    bind(range.layout, *m_noObject);
    VKM_GL_CHECK(
        glDrawElementsBaseVertex(
            GL_TRIANGLES,
            static_cast<GLsizei>(range.indexCount),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(uintptr_t{range.firstIndex} * sizeof(uint32_t)),
            static_cast<GLint>(range.firstVertex)
        )
    );
}

void GLMeshPool::drawIndirect(
    VertexLayout layout,
    const Vkm::GL::VertexBuffer& instances,
    const Vkm::GL::VertexBuffer& commands,
    uint32_t first,
    uint32_t count
) const {
    if (count == 0 || !m_indices) return;
    bind(layout, instances);
    commands.bind(GL_DRAW_INDIRECT_BUFFER);
    VKM_GL_CHECK(
        glMultiDrawElementsIndirect(
            GL_TRIANGLES,
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(uintptr_t{first} * sizeof(DrawCommand)),
            static_cast<GLsizei>(count),
            0
        )
    );
}

void GLMeshPool::bind(VertexLayout layout, const Vkm::GL::VertexBuffer& instances) const {
    m_arrays[static_cast<size_t>(layout)].bind();
    VKM_GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indices->getID()));
    VKM_GL_CHECK(glBindVertexBuffer(INSTANCE_BINDING, instances.getID(), 0, sizeof(uint32_t)));
}

void GLMeshPool::wire() {
    for (size_t layout = 0; layout < static_cast<size_t>(VertexLayout::Count); ++layout) {
        if (!m_vertices[layout]) continue;
        m_arrays[layout].bind();
        VKM_GL_CHECK(glBindVertexBuffer(VERTEX_BINDING, m_vertices[layout]->getID(), 0, sizeof(Vertex)));
        if (layout == SKINNED) {
            VKM_GL_CHECK(glBindVertexBuffer(SKIN_BINDING, m_skin->getID(), 0, sizeof(SkinVertex)));
        }
    }
    VKM_GL_CHECK(glBindVertexArray(0));
}

} // namespace Vkm::Engine
