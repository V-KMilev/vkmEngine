#include "gpu_material.h"

namespace Engine {
    GPUMaterial::GPUMaterial(
        const CPUMaterial& cpuMaterial
    ) {
        updateFromCPU(cpuMaterial);
    }

    void GPUMaterial::bind() const {
        if (m_uniformBuffer) {
            m_uniformBuffer->bindBase(0); // Bind to binding point 0
        }
        // Bind textures to slots if needed
        if (m_diffuseTexture) {
            m_diffuseTexture->bindSlot(0);
        }
        if (m_normalTexture) {
            m_normalTexture->bindSlot(1);
        }
        if (m_specularTexture) {
            m_specularTexture->bindSlot(2);
        }
    }

    void GPUMaterial::unbind() const {
        // Unbind textures
        if (m_diffuseTexture) {
            m_diffuseTexture->unbind();
        }
        if (m_normalTexture) {
            m_normalTexture->unbind();
        }
        if (m_specularTexture) {
            m_specularTexture->unbind();
        }
        if (m_uniformBuffer) {
            m_uniformBuffer->unbind();
        }
    }

    void GPUMaterial::updateFromCPU(const CPUMaterial& cpuMaterial) {
        const auto& props = cpuMaterial.getProperties();
        // Create or update uniform buffer
        struct MaterialData {
            glm::vec3 diffuse;
            float roughness;
        };
        MaterialData data = {props.diffuse, props.roughness};
        if (!m_uniformBuffer) {
            m_uniformBuffer = std::make_unique<Core::UniformBuffer>(&data, sizeof(MaterialData), GL_DYNAMIC_DRAW);
        } else {
            m_uniformBuffer->update(&data, sizeof(MaterialData));
        }

        // Load textures
        if (!props.diffuseTexturePath.empty()) {
            m_diffuseTexture = std::make_shared<Core::Texture2D>(props.diffuseTexturePath);
        }
        if (!props.normalTexturePath.empty()) {
            m_normalTexture = std::make_shared<Core::Texture2D>(props.normalTexturePath);
        }
        if (!props.specularTexturePath.empty()) {
            m_specularTexture = std::make_shared<Core::Texture2D>(props.specularTexturePath);
        }
    }

    const std::shared_ptr<Core::Texture2D>& GPUMaterial::getDiffuseTexture() const {
        return m_diffuseTexture;
    }

    const std::shared_ptr<Core::Texture2D>& GPUMaterial::getNormalTexture() const {
        return m_normalTexture;
    }

    const std::shared_ptr<Core::Texture2D>& GPUMaterial::getSpecularTexture() const {
        return m_specularTexture;
    }
} // namespace Engine
