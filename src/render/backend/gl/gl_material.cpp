#include "gl_material.h"

#include "logger.h"

#include "gl_uniform_buffer.h"

#include "resource.h"
#include "resource_manager.h"

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

// static_assert(sizeof(MaterialUBOData) % 16 == 0, "MaterialUBOData must be 16-byte aligned for std140");


GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() {
    m_ubo.reset();

    LOG_TRACE("Destroying GLMaterial");
}

void GLMaterial::update(const MaterialAsset& material) {
    // Base PBR properties
    MaterialUBOData uboData;

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

    // Texture presence flags and slots
    uboData.hasAlbedoTexture = (material.albedoTexture.value != 0) ? 1 : 0;
    uboData.albedoTextureSlot = MaterialTextureSlots::Albedo;

    uboData.hasNormalTexture = (material.normalTexture.value != 0) ? 1 : 0;
    uboData.normalTextureSlot = MaterialTextureSlots::Normal;

    uboData.hasMetallicRoughnessTexture = (material.metallicRoughnessTexture.value != 0) ? 1 : 0;
    uboData.metallicRoughnessTextureSlot = MaterialTextureSlots::MetallicRoughness;

    uboData.hasMetallicTexture = (material.metallicTexture.value != 0) ? 1 : 0;
    uboData.metallicTextureSlot = MaterialTextureSlots::Metallic;

    uboData.hasRoughnessTexture = (material.roughnessTexture.value != 0) ? 1 : 0;
    uboData.roughnessTextureSlot = MaterialTextureSlots::Roughness;

    uboData.hasAOTexture = (material.aoTexture.value != 0) ? 1 : 0;
    uboData.aoTextureSlot = MaterialTextureSlots::AO;

    uboData.hasAOMetallicRoughnessTexture = (material.aoMetallicRoughnessTexture.value != 0) ? 1 : 0;
    uboData.aoMetallicRoughnessTextureSlot = MaterialTextureSlots::MetallicRoughness;

    uboData.hasEmissionTexture = (material.emissionTexture.value != 0) ? 1 : 0;
    uboData.emissionTextureSlot = MaterialTextureSlots::Emission;

    uboData.hasHeightTexture = (material.heightTexture.value != 0) ? 1 : 0;
    uboData.heightTextureSlot = MaterialTextureSlots::Height;

    uboData.hasClearcoatTexture = (material.clearcoatTexture.value != 0) ? 1 : 0;
    uboData.clearcoatTextureSlot = MaterialTextureSlots::Clearcoat;

    uboData.hasTransmissionTexture = (material.transmissionTexture.value != 0) ? 1 : 0;
    uboData.transmissionTextureSlot = MaterialTextureSlots::Transmission;

    m_ubo = std::make_unique<Core::UniformBuffer>(
        &uboData,
        sizeof(MaterialUBOData),
        GL_DYNAMIC_DRAW
    );
}

void GLMaterial::bind(uint32_t bindingPoint) const {
    // Bind the uniform buffer to the specified binding point
    m_ubo->bindBase(bindingPoint);
}

} // namespace Engine

