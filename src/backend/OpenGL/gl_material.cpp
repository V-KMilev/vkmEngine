#include "gl_material.h"

#include <iterator>

#include "logger.h"

#include "gl_uniform_buffer.h"

#include "material_asset.h"
#include "gl_view.h"
#include "gl_texture.h"

namespace Engine {

// Texture slot assignments for PBR materials
namespace MaterialTextureSlots {
    constexpr uint32_t Albedo               = 0;
    constexpr uint32_t Normal               = 1;
    constexpr uint32_t MetallicRoughness    = 2;
    constexpr uint32_t AO                   = 3;
    constexpr uint32_t Emission             = 4;
    constexpr uint32_t Height               = 5;
    constexpr uint32_t Clearcoat            = 6;
    constexpr uint32_t Transmission         = 7;
    constexpr uint32_t Metallic             = 8;  // If separate from MetallicRoughness
    constexpr uint32_t Roughness            = 9;  // If separate from MetallicRoughness
}

namespace {
    /**
     * @brief Texture mapping information for material textures.
     * 
     * Maps texture handles from MaterialAsset to their corresponding bit flags
     * and texture slots. This eliminates duplication and makes it easy to add new textures.
     */
    struct TextureMapping {
        TextureHandle MaterialAsset::*handlePtr;
        MaterialTextureFlags flag;
        uint32_t slot;
    };

    /**
     * @brief Table of all texture mappings for PBR materials.
     * 
     * This table defines how each texture type maps to bit flags and texture slots.
     * Adding a new texture type only requires adding an entry here.
     */
    constexpr TextureMapping g_textureMappings[] = {
        {&MaterialAsset::albedoTexture,              MaterialTextureFlags::Albedo,              MaterialTextureSlots::Albedo},
        {&MaterialAsset::normalTexture,              MaterialTextureFlags::Normal,              MaterialTextureSlots::Normal},
        {&MaterialAsset::metallicRoughnessTexture,   MaterialTextureFlags::MetallicRoughness,   MaterialTextureSlots::MetallicRoughness},
        {&MaterialAsset::metallicTexture,            MaterialTextureFlags::Metallic,            MaterialTextureSlots::Metallic},
        {&MaterialAsset::roughnessTexture,           MaterialTextureFlags::Roughness,           MaterialTextureSlots::Roughness},
        {&MaterialAsset::aoTexture,                  MaterialTextureFlags::AO,                  MaterialTextureSlots::AO},
        {&MaterialAsset::aoMetallicRoughnessTexture, MaterialTextureFlags::AOMetallicRoughness, MaterialTextureSlots::MetallicRoughness},
        {&MaterialAsset::emissionTexture,            MaterialTextureFlags::Emission,            MaterialTextureSlots::Emission},
        {&MaterialAsset::heightTexture,              MaterialTextureFlags::Height,              MaterialTextureSlots::Height},
        {&MaterialAsset::clearcoatTexture,           MaterialTextureFlags::Clearcoat,           MaterialTextureSlots::Clearcoat},
        {&MaterialAsset::transmissionTexture,        MaterialTextureFlags::Transmission,        MaterialTextureSlots::Transmission},
    };
}

// Check if MaterialUBOData is 16-byte aligned for std140
static_assert(sizeof(MaterialUBOData) % 16 == 0, "MaterialUBOData must be 16-byte aligned for std140");


GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() {
    m_ubo.reset();

    LOG_TRACE("Destroying GLMaterial");
}

void GLMaterial::update(const MaterialAsset& material) {
    // Build UBO data from material asset
    MaterialUBOData uboData;

    // Base PBR properties
    uboData.albedo = material.albedo;
    uboData.emission = material.emission;
    uboData.metallic = material.metallic;
    uboData.roughness = material.roughness;

    // Essential for raytracing and advanced PBR
    uboData.ior = material.ior;
    uboData.transmission = material.transmission;
    uboData.alpha = material.alpha;
    uboData.ao = material.ao;

    // Advanced PBR properties
    uboData.clearcoat = material.clearcoat;
    uboData.clearcoatRoughness = material.clearcoatRoughness;
    uboData.anisotropy = material.anisotropy;
    uboData.anisotropyDirection = material.anisotropyDirection;

    // Subsurface scattering
    uboData.subsurface = material.subsurface;
    uboData.subsurfaceColor = material.subsurfaceColor;

    // Height/Displacement
    uboData.heightScale = material.heightScale;

    // Process all texture mappings in a single loop
    // Build texture flags bitfield and bindings list
    m_textureBindings.clear();
    m_textureBindings.reserve(std::size(g_textureMappings));

    uint32_t textureFlags = 0;

    for (const auto& mapping : g_textureMappings) {
        const TextureHandle& handle = material.*mapping.handlePtr;
        const bool hasTexture = (handle.value != 0);

        // Set bit flag if texture is present
        if (hasTexture) {
            textureFlags |= static_cast<uint32_t>(mapping.flag);
            m_textureBindings.push_back({handle, mapping.slot});
        }
    }

    uboData.textureFlags = static_cast<int>(textureFlags);

    // Update or create UBO
    if (m_ubo) {
        m_ubo->update(&uboData, sizeof(MaterialUBOData));
    } else {
        m_ubo = std::make_unique<Core::UniformBuffer>(&uboData, sizeof(MaterialUBOData));
    }
}

void GLMaterial::bind(uint32_t bindingPoint) const {
    // Bind the uniform buffer to the specified binding point
    m_ubo->bindBase(bindingPoint);
}

void GLMaterial::bindTextures(const GLView& view) const {
    // Bind all textures in the bindings list
    // Note: We don't need to check handle.value here since we only add valid handles to the list
    for (const auto& binding : m_textureBindings) {
        view.getTexture(binding.handle).bind(binding.slot);
    }
}

} // namespace Engine

