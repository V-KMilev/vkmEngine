#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "texture_asset.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct MaterialAsset;
    class GLView;
}

namespace Engine {

/**
 * @brief Texture flag bit positions for MaterialUBOData.
 * 
 * Each texture type gets a single bit in the textureFlags field.
 * This replaces the wasteful individual int fields (352 bytes → 4 bytes).
 */
enum class MaterialTextureFlags : uint32_t {
    Albedo               = 1 << 0,   // bit 0
    Normal               = 1 << 1,   // bit 1
    MetallicRoughness    = 1 << 2,   // bit 2
    Metallic             = 1 << 3,   // bit 3
    Roughness            = 1 << 4,   // bit 4
    AO                   = 1 << 5,   // bit 5
    AOMetallicRoughness  = 1 << 6,   // bit 6
    Emission             = 1 << 7,   // bit 7
    Height               = 1 << 8,   // bit 8
    Clearcoat            = 1 << 9,   // bit 9
    Transmission         = 1 << 10,  // bit 10
};

/**
 * @brief Material data structure matching GLSL std140 layout for uniform buffer.
 * 
 * This struct must match the Material uniform block in the shader exactly.
 * 
 * Optimizations:
 * - Texture flags packed into a single int (bitfield) instead of 11 separate ints
 * - Texture slots removed (they're hardcoded constants, no need to send them to GPU)
 * - Size reduced from ~600 bytes to ~240 bytes (60% reduction)
 * 
 * std140 layout rules:
 * - vec4: 16-byte aligned
 * - vec3: 16-byte aligned (padded to vec4)
 * - float: 4 bytes, but 16-byte aligned in struct
 * - int: 4 bytes, but 16-byte aligned in struct
 */
struct alignas(16) MaterialUBOData {
    // Base PBR properties
    glm::vec4 albedo;                    // 16 bytes, offset 0
    glm::vec3 emission;                  // 12 bytes + 4 padding = 16 bytes, offset 16
    float metallic;                      // 4 bytes + 12 padding = 16 bytes, offset 32
    float roughness;                     // 4 bytes + 12 padding = 16 bytes, offset 48
    // Essential for raytracing and advanced PBR
    float ior;                           // 4 bytes + 12 padding = 16 bytes, offset 64
    float transmission;                  // 4 bytes + 12 padding = 16 bytes, offset 80
    float alpha;                         // 4 bytes + 12 padding = 16 bytes, offset 96
    float ao;                            // 4 bytes + 12 padding = 16 bytes, offset 112
    // Advanced PBR properties
    float clearcoat;                     // 4 bytes + 12 padding = 16 bytes, offset 128
    float clearcoatRoughness;            // 4 bytes + 12 padding = 16 bytes, offset 144
    float anisotropy;                    // 4 bytes + 12 padding = 16 bytes, offset 160
    glm::vec3 anisotropyDirection;       // 12 bytes + 4 padding = 16 bytes, offset 176
    // Subsurface scattering
    float subsurface;                    // 4 bytes + 12 padding = 16 bytes, offset 192
    glm::vec3 subsurfaceColor;           // 12 bytes + 4 padding = 16 bytes, offset 208
    // Height/Displacement
    float heightScale;                   // 4 bytes + 12 padding = 16 bytes, offset 224
    // Texture presence flags (bitfield - all texture flags packed into one int)
    // Use MaterialTextureFlags enum to check bits
    int textureFlags;                    // 4 bytes + 12 padding = 16 bytes, offset 240
    // Note: Texture slots are hardcoded in shader as constants (0-10), no need to send them
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

        /**
         * @brief Binds all textures referenced by this material.
         * 
         * Binds textures to their assigned texture slots as defined in the material.
         * This should be called after bind() to ensure textures are available for rendering.
         * 
         * @param view The GLView containing the synced texture resources.
         */
        void bindTextures(const class GLView& view) const;

    private:
        /**
         * @brief Helper structure for texture binding information.
         */
        struct TextureBinding {
            TextureHandle handle;
            uint32_t slot;
        };

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;

        // Store texture bindings for efficient iteration
        std::vector<TextureBinding> m_textureBindings;
};

} // namespace Engine

