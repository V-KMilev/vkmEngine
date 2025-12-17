#pragma once

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

#include "resource.h"

namespace Core {
    class Shader;
    class UniformBuffer;
}

namespace Engine {
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Material data structure matching GLSL std140 layout for uniform buffer.
 * 
 * This struct must match the Material uniform block in the shader exactly.
 * std140 layout rules:
 * - vec4: 16-byte aligned
 * - vec3: 16-byte aligned (padded to vec4)
 * - float: 4 bytes, but 16-byte aligned in struct
 * - int: 4 bytes, but 16-byte aligned in struct
 */
struct alignas(16) MaterialUBOData {
    // Base PBR properties (vec4 = 16 bytes)
    glm::vec4 albedo;              // 16 bytes, offset 0
    glm::vec3 emission;             // 12 bytes + 4 padding = 16 bytes, offset 16
    float metallic;                 // 4 bytes + 12 padding = 16 bytes, offset 32
    float roughness;                // 4 bytes + 12 padding = 16 bytes, offset 48

    // Essential for raytracing and advanced PBR
    float ior;                      // 4 bytes + 12 padding = 16 bytes, offset 64
    float transmission;             // 4 bytes + 12 padding = 16 bytes, offset 80
    float alpha;                    // 4 bytes + 12 padding = 16 bytes, offset 96
    float ao;                       // 4 bytes + 12 padding = 16 bytes, offset 112

    // Advanced PBR properties
    float clearcoat;                // 4 bytes + 12 padding = 16 bytes, offset 128
    float clearcoatRoughness;      // 4 bytes + 12 padding = 16 bytes, offset 144
    float anisotropy;               // 4 bytes + 12 padding = 16 bytes, offset 160
    glm::vec3 anisotropyDirection; // 12 bytes + 4 padding = 16 bytes, offset 176

    // Subsurface scattering
    float subsurface;               // 4 bytes + 12 padding = 16 bytes, offset 192
    glm::vec3 subsurfaceColor;     // 12 bytes + 4 padding = 16 bytes, offset 208

    // Height/Displacement
    float heightScale;              // 4 bytes + 12 padding = 16 bytes, offset 224

    // Texture presence flags (packed into vec4s for alignment)
    // Each int is 4 bytes, but std140 requires 16-byte alignment
    int hasAlbedoTexture;           // 4 bytes + 12 padding = 16 bytes, offset 240
    int hasNormalTexture;           // 4 bytes + 12 padding = 16 bytes, offset 256
    int hasMetallicRoughnessTexture;// 4 bytes + 12 padding = 16 bytes, offset 272
    int hasMetallicTexture;         // 4 bytes + 12 padding = 16 bytes, offset 288
    int hasRoughnessTexture;        // 4 bytes + 12 padding = 16 bytes, offset 304
    int hasAOTexture;               // 4 bytes + 12 padding = 16 bytes, offset 320
    int hasAOMetallicRoughnessTexture; // 4 bytes + 12 padding = 16 bytes, offset 336
    int hasEmissionTexture;         // 4 bytes + 12 padding = 16 bytes, offset 352
    int hasHeightTexture;           // 4 bytes + 12 padding = 16 bytes, offset 368
    int hasClearcoatTexture;        // 4 bytes + 12 padding = 16 bytes, offset 384
    int hasTransmissionTexture;     // 4 bytes + 12 padding = 16 bytes, offset 400

    // Texture sampler slots (packed similarly)
    int albedoTextureSlot;          // 4 bytes + 12 padding = 16 bytes, offset 416
    int normalTextureSlot;          // 4 bytes + 12 padding = 16 bytes, offset 432
    int metallicRoughnessTextureSlot;// 4 bytes + 12 padding = 16 bytes, offset 448
    int metallicTextureSlot;        // 4 bytes + 12 padding = 16 bytes, offset 464
    int roughnessTextureSlot;      // 4 bytes + 12 padding = 16 bytes, offset 480
    int aoTextureSlot;              // 4 bytes + 12 padding = 16 bytes, offset 496
    int aoMetallicRoughnessTextureSlot; // 4 bytes + 12 padding = 16 bytes, offset 512
    int emissionTextureSlot;        // 4 bytes + 12 padding = 16 bytes, offset 528
    int heightTextureSlot;          // 4 bytes + 12 padding = 16 bytes, offset 544
    int clearcoatTextureSlot;       // 4 bytes + 12 padding = 16 bytes, offset 560
    int transmissionTextureSlot;    // 4 bytes + 12 padding = 16 bytes, offset 576

    // Explicit padding to ensure 16-byte alignment for std140
    // The compiler adds padding differently than our manual calculations
    // Add padding to round up to the next multiple of 16
    char _padding[8];               // 8 bytes explicit padding
};

/**
 * @brief Encapsulates an OpenGL material, managing PBR properties and texture bindings.
 *
 * GLMaterial maintains material properties in a uniform buffer object (UBO) for efficient
 * GPU access. It provides methods to bind textures and update the UBO for PBR rendering.
 * It prohibits copy/move semantics to ensure unique OpenGL state ownership.
 */
class GLMaterial {
    public:
        GLMaterial() = delete;
        ~GLMaterial();

        GLMaterial(const GLMaterial& other) = delete;
        GLMaterial& operator=(const GLMaterial& other) = delete;

        GLMaterial(GLMaterial && other) = delete;
        GLMaterial& operator=(GLMaterial && other) = delete;

        /**
         * @brief Constructs a material from the provided asset.
         * @param material Reference to the material asset.
         */
        GLMaterial(const MaterialAsset& material);

    public:
        /**
         * @brief Updates the material properties from a new asset.
         * @param material Reference to the new material asset.
         */
        void update(const MaterialAsset& material);

        /**
         * @brief Binds the material uniform buffer.
         * 
         * Binds the material UBO to the specified binding point. The UBO contains
         * all material properties and texture presence flags.
         * 
         * @param bindingPoint The UBO binding point index (default: 0).
         */
        void bind(uint32_t bindingPoint = 0) const;

    private:
        // Uniform buffer object for GPU access
        std::unique_ptr<Core::UniformBuffer> m_ubo;
};

} // namespace Engine

