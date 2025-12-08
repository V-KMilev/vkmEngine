#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @struct Vertex
 * @brief Represents a single vertex in a mesh, including position, normal vector, and texture coordinate (UV).
 *
 * Members:
 *   - position: 3D position of the vertex (typically in model space).
 *   - normal:   Normal vector at the vertex, used for lighting computations.
 *   - uv:       2D texture coordinate of the vertex.
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

/**
 * @class CPUMesh
 * @brief Stores and manages mesh geometry data (vertices & indices) on the CPU side.
 *
 * CPUMesh allows loading mesh data from files or directly from arrays, and exposes methods
 * to set and access the geometry (vertex and index data). Copy and move are disabled for resource safety.
 */
class CPUMesh {
    public:
        CPUMesh() = default;
        ~CPUMesh() = default;

        CPUMesh(const CPUMesh& other) = delete;
        CPUMesh& operator=(const CPUMesh& other) = delete;

        CPUMesh(CPUMesh && other) = delete;
        CPUMesh& operator=(CPUMesh && other) = delete;

        CPUMesh(const std::string& filePath);
        CPUMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    public:
        /**
         * @brief Loads mesh data from a file.
         * @param filePath Path to the mesh file.
         * @return True if loading was successful, false otherwise.
         */
        bool loadFromFile(const std::string& filePath);

        /**
         * @brief Sets the mesh data directly.
         * @param vertices The new vertex array.
         * @param indices The new index array.
         */
        void setData(
            const std::vector<Vertex>& vertices = {},
            const std::vector<uint32_t>& indices = {}
        );

        /**
         * @brief Gets a read-only reference to the vertex data.
         * @return Reference to the internal vertex vector.
         */
        const std::vector<Vertex>& getVertices() const { return m_vertices; }

        /**
         * @brief Gets a read-only reference to the index data.
         * @return Reference to the internal index vector.
         */
        const std::vector<uint32_t>& getIndices() const { return m_indices; }

    private:
        std::vector<Vertex> m_vertices;
        std::vector<uint32_t> m_indices;
};

} // namespace Engine
