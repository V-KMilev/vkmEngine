#include "cpu_mesh.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Engine {

CPUMesh::CPUMesh(const std::string& filePath) {
    loadFromFile(filePath);
}

CPUMesh::CPUMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices
) : m_vertices(vertices),
    m_indices(indices) {}

bool CPUMesh::loadFromFile(const std::string& filePath) {
    // TODO: Implement actual file loading.

    // Dummy parsing: Cube vertices (positions, normals, uvs)
    m_vertices = {
        // Front face
        {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec2(0.0f, 0.0f)}, // 0
        {glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec2(1.0f, 0.0f)}, // 1
        {glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec2(1.0f, 1.0f)}, // 2
        {glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec2(0.0f, 1.0f)}, // 3

        // Back face
        {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(1.0f, 0.0f)}, // 4
        {glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(0.0f, 0.0f)}, // 5
        {glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(0.0f, 1.0f)}, // 6
        {glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec2(1.0f, 1.0f)}, // 7

        // Left face
        {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-1.0f,  0.0f, 0.0f), glm::vec2(0.0f, 0.0f)}, // 8
        {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec3(-1.0f,  0.0f, 0.0f), glm::vec2(1.0f, 0.0f)}, // 9
        {glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec3(-1.0f,  0.0f, 0.0f), glm::vec2(1.0f, 1.0f)}, // 10
        {glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec3(-1.0f,  0.0f, 0.0f), glm::vec2(0.0f, 1.0f)}, // 11

        // Right face
        {glm::vec3(0.5f, -0.5f,  0.5f), glm::vec3(1.0f,  0.0f, 0.0f), glm::vec2(0.0f, 0.0f)}, // 12
        {glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(1.0f,  0.0f, 0.0f), glm::vec2(1.0f, 0.0f)}, // 13
        {glm::vec3(0.5f,  0.5f, -0.5f), glm::vec3(1.0f,  0.0f, 0.0f), glm::vec2(1.0f, 1.0f)}, // 14
        {glm::vec3(0.5f,  0.5f,  0.5f), glm::vec3(1.0f,  0.0f, 0.0f), glm::vec2(0.0f, 1.0f)}, // 15

        // Top face
        {glm::vec3(-0.5f, 0.5f,  0.5f), glm::vec3(0.0f,  1.0f, 0.0f), glm::vec2(0.0f, 0.0f)}, // 16
        {glm::vec3(0.5f, 0.5f,  0.5f),  glm::vec3(0.0f,  1.0f, 0.0f), glm::vec2(1.0f, 0.0f)}, // 17
        {glm::vec3(0.5f, 0.5f, -0.5f),  glm::vec3(0.0f,  1.0f, 0.0f), glm::vec2(1.0f, 1.0f)}, // 18
        {glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.0f,  1.0f, 0.0f), glm::vec2(0.0f, 1.0f)}, // 19

        // Bottom face
        {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec2(0.0f, 0.0f)}, // 20
        {glm::vec3(0.5f, -0.5f, -0.5f),  glm::vec3(0.0f, -1.0f, 0.0f), glm::vec2(1.0f, 0.0f)}, // 21
        {glm::vec3(0.5f, -0.5f,  0.5f),  glm::vec3(0.0f, -1.0f, 0.0f), glm::vec2(1.0f, 1.0f)}, // 22
        {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec2(0.0f, 1.0f)}, // 23
    };

    // Cube indices (two triangles per face)
    m_indices = {
        // Front face
        0, 1, 2, 2, 3, 0,
        // Back face
        4, 5, 6, 6, 7, 4,
        // Left face
        8, 9,10,10,11, 8,
        // Right face
        12,13,14,14,15,12,
        // Top face
        16,17,18,18,19,16,
        // Bottom face
        20,21,22,22,23,20
    };

    return true;
}

void CPUMesh::setVertices(const std::vector<Vertex>& vertices) { m_vertices = vertices; }
void CPUMesh::setIndices(const std::vector<uint32_t>& indices) { m_indices = indices; }

const std::vector<Vertex>& CPUMesh::getVertices() const { return m_vertices; }
const std::vector<uint32_t>& CPUMesh::getIndices() const { return m_indices; }
} // namespace Engine
