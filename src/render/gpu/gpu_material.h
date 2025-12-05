#pragma once

#include <memory>
#include <glm/glm.hpp>

#include "gl_uniform_buffer.h"
#include "gl_texture.h"

#include "cpu_material.h"

namespace Engine {

class GPUMaterial {
    public:
        GPUMaterial() = delete;
        ~GPUMaterial() = default;

        GPUMaterial(const GPUMaterial& other) = delete;
        GPUMaterial& operator=(const GPUMaterial& other) = delete;

        GPUMaterial(GPUMaterial&& other) = delete;
        GPUMaterial& operator=(GPUMaterial&& other) = delete;

        explicit GPUMaterial(const CPUMaterial& cpuMaterial);

    public:
        void bind() const;
        void unbind() const;

        void updateFromCPU(const CPUMaterial& cpuMaterial);

        const std::shared_ptr<Core::Texture2D>& getDiffuseTexture() const;
        const std::shared_ptr<Core::Texture2D>& getNormalTexture() const;
        const std::shared_ptr<Core::Texture2D>& getSpecularTexture() const;

    private:
        std::unique_ptr<Core::UniformBuffer> m_uniformBuffer;
        std::shared_ptr<Core::Texture2D> m_diffuseTexture;
        std::shared_ptr<Core::Texture2D> m_normalTexture;
        std::shared_ptr<Core::Texture2D> m_specularTexture;
};
} // namespace Engine
