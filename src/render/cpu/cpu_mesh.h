#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <glm/glm.hpp>

namespace Engine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

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
        bool loadFromFile(const std::string& filePath);
        void setVertices(const std::vector<Vertex>& vertices);
        void setIndices(const std::vector<uint32_t>& indices);

        const std::vector<Vertex>& getVertices() const;
        const std::vector<uint32_t>& getIndices() const;

    private:
        std::vector<Vertex> m_vertices;
        std::vector<uint32_t> m_indices;
};

} // namespace Engine
