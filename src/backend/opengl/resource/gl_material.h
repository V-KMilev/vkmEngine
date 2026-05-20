#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "config/gl_config.h"
#include "resource/texture_asset.h"

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
 * This replaces the wasteful individual int fields (352 bytes -> 4 bytes).
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
/**
 * @brief Std140-friendly material UBO with explicit padding (dev-friendly fields).
 *
 * Layout (std140):
 * 0   : vec4  albedo
 * 16  : vec3  emission + pad0
 * 32  : float metallic, roughness, ior, transmission
 * 48  : float alpha, ao, clearcoat, clearcoatRoughness
 * 64  : float anisotropy, sheenColor[3]
 * 80  : vec3  anisotropyDirection + pad2
 * 96  : float subsurface, pad3[3] (pad to next vec3)
 * 112 : vec3  subsurfaceColor + pad4
 * 128 : float heightScale, normalScale, int textureFlags, float sheenRoughness
 * 144 : vec3  attenuationColor + float attenuationDistance
 * 160 : float thicknessFactor + pad6[3]
 * Total: 176 bytes
 */
struct alignas(16) MaterialUBOData {
    glm::vec4 albedo;                    // offset 0
    alignas(16) glm::vec3 emission;      // offset 16
    float pad0;                          // offset 28

    float metallic;                      // offset 32
    float roughness;                     // offset 36
    float ior;                           // offset 40
    float transmission;                  // offset 44

    float alpha;                         // offset 48
    float ao;                            // offset 52
    float clearcoat;                     // offset 56
    float clearcoatRoughness;            // offset 60

    float anisotropy;                    // offset 64
    float sheenColor[3];                 // offset 68 (was pad1; r,g,b scalars)

    alignas(16) glm::vec3 anisotropyDirection; // offset 80
    float pad2;                          // offset 92

    float subsurface;                    // offset 96
    float pad3[3];                       // offset 100 (pad to 112)

    alignas(16) glm::vec3 subsurfaceColor; // offset 112
    float pad4;                          // offset 124

    float heightScale;                   // offset 128
    float normalScale;                   // offset 132
    int textureFlags;                    // offset 136
    float sheenRoughness;                // offset 140 (was pad5)

    // KHR_materials_volume. attenuationDistance packs into the .w of the
    // attenuationColor vec4 slot so the std140 padding does double duty.
    alignas(16) glm::vec3 attenuationColor;    // offset 144
    float attenuationDistance;                 // offset 156

    float thicknessFactor;               // offset 160
    float alphaCutoff;                   // offset 164 (was pad6[0])
    float pad6[2];                       // offset 168 (pad to 176)
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
         * @param bindingPoint The UBO binding point index.
         */
        void bind(uint32_t bindingPoint = GLConfig::UBOBindingPoints::Material) const;

        /**
         * @brief Binds all textures referenced by this material.
         * 
         * Binds textures to their assigned texture slots as defined in the material.
         * This should be called after bind() to ensure textures are available for rendering.
         * 
         * @param view The GLView containing the synced texture resources.
         */
        void bindTextures(const class GLView& view) const;

    public:
        /**
         * @brief Helper structure for texture binding information.
         */
        struct TextureBinding {
            TextureHandle handle;
            uint32_t slot;
        };

        /**
         * @brief Returns the stored texture bindings for resource tracking.
         */
        const std::vector<TextureBinding>& getTextureBindings() const { return m_textureBindings; }

        /**
         * @brief Optional-feature bitset (MaterialFeature) cached at update().
         *
         * Drives the per-material shader variant lookup in GLForwardPass:
         * each unique flag combination compiles its own PBR program with the
         * matching #ifdef HAS_X gates active and everything else dead-coded
         * out. Recomputed whenever the asset version bumps - editor edits
         * pick up new features automatically.
         */
        uint32_t getFeatureFlags() const { return m_featureFlags; }

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;

        // Store texture bindings for efficient iteration
        std::vector<TextureBinding> m_textureBindings;

        uint32_t m_featureFlags = 0;
};

} // namespace Engine

