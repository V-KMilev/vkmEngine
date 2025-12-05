#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine {
struct MaterialProperties {
    glm::vec3 diffuse = glm::vec3(1.0f);
    float roughness = 0.5f;

    std::string diffuseTexturePath;
    std::string normalTexturePath;
    std::string specularTexturePath;
};

class CPUMaterial {
    public:
        CPUMaterial() = default;
        ~CPUMaterial() = default;

        CPUMaterial(const CPUMaterial& other) = delete;
        CPUMaterial& operator=(const CPUMaterial& other) = delete;

        CPUMaterial(CPUMaterial && other) = delete;
        CPUMaterial& operator=(CPUMaterial && other) = delete;

        CPUMaterial(const MaterialProperties& properties);

    public:
        void setProperties(const MaterialProperties& properties);
        const MaterialProperties& getProperties() const;
        bool loadFromFile(const std::string& filePath);

    private:
        MaterialProperties m_properties;
};
} // namespace Engine
