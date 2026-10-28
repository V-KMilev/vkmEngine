#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "asset/gl_mesh_pool.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief GPU copy of a mesh: its range in a GLMeshPool.
 *
 * Meshes of a layout share the pool's buffers and one vertex array, so many go out in one
 * multi-draw (GLDrawList). The pool outlives every mesh in it. A skinned mesh carries its rig
 * binding in a second stream (ATTR_BONES, ATTR_WEIGHTS), so a rock pays nothing for it.
 */
class GLMesh {
    public:
        GLMesh(GLMeshPool& pool, const MeshAsset& mesh);
        ~GLMesh();

        GLMesh(const GLMesh& other) = delete;
        GLMesh& operator=(const GLMesh& other) = delete;

        GLMesh(GLMesh && other) = delete;
        GLMesh& operator=(GLMesh && other) = delete;

    public:
        void update(const MeshAsset& mesh);

        /// Draw once, for a program with no instance attribute.
        void draw() const { m_pool.draw(m_range); }

        /**
         * @brief The command drawing @p instances of this mesh from entry @p baseInstance of a list.
         *
         * The list holds a uint per instance, read at location 4 with divisor 1: the instance's
         * slot in the storage buffers the program reads transforms from.
         *
         * @param instances    Instances to draw.
         * @param baseInstance First entry of the instance list this command reads.
         * @return The record for GLDrawList's command list.
         */
        DrawCommand command(uint32_t instances, uint32_t baseInstance) const {
            return {
                m_range.indexCount,
                instances,
                m_range.firstIndex,
                static_cast<int32_t>(m_range.firstVertex),
                baseInstance
            };
        }

        /// The vertex array that draws it, in its pool.
        VertexLayout layout() const { return m_range.layout; }

        /// The pool it lives in, whose vertex arrays a multi-draw of it binds.
        const GLMeshPool& pool() const { return m_pool; }

        /**
         * @brief Whether this mesh carries a rig binding at locations 8/9.
         *
         * The program is chosen from here: a run without a skin stream can never be drawn by a
         * program that reads one.
         *
         * @return True for the skinned layout.
         */
        bool isSkinned() const { return m_range.layout == VertexLayout::Skinned; }

        /**
         * @brief Which upload these buffers hold, unique across every GLMesh.
         *
         * Fresh per construction and update(), so it names the geometry, not the asset: a re-cook
         * under the same handle, or a mirror rebuilt after the graph was replaced, gets a new
         * number. A cache of a mesh's shape keys on this, not the handle.
         *
         * @return The upload id, never zero once constructed.
         */
        uint64_t uploadId() const { return m_uploadId; }

        /**
         * @brief The surface its lightmap holds (MeshAsset::lightmapUVs), seen along X, Y and Z, in
         *        its own units: what a lightmap is sized by; zero when it carries none.
         *
         * Each axis's share scales by how far a model stretches the plane across it, so a box
         * stretched into a floor is sized as the floor it is.
         *
         * @return The areas, model space.
         */
        const glm::vec3& lightmapArea() const { return m_lightmapArea; }

        /// Whether it carries lightmap coordinates a lightmap can be read through.
        bool lightmapped() const { return m_lightmapArea != glm::vec3(0.0f); }

    private:
        GLMeshPool& m_pool;
        MeshRange   m_range;
        uint64_t    m_uploadId     = 0;
        glm::vec3   m_lightmapArea{0.0f};
};

} // namespace Vkm::Engine
