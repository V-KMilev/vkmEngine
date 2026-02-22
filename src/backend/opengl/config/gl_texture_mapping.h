#pragma once

#include <cstdint>
#include <array>

#include "resource/material_asset.h"
#include "gl_config.h"
#include "resource/gl_material.h"

namespace Engine {

/**
 * @brief Texture mapping information for material textures.
 * 
 * Maps texture handles from MaterialAsset to their corresponding bit flags
 * and texture slots. This centralizes the mapping logic to eliminate duplication.
 */
struct TextureMapping {
    TextureHandle MaterialAsset::*handlePtr;  ///< Pointer to texture handle in MaterialAsset
    MaterialTextureFlags flag;                 ///< Bit flag for this texture type
    uint32_t slot;                             ///< Texture unit slot for binding
};

/**
 * @brief Centralized table of all texture mappings for PBR materials.
 * 
 * This table defines how each texture type maps to bit flags and texture slots.
 * Used by both GLMaterial and GLView for consistent texture handling.
 * 
 * Benefits:
 * - Single source of truth for texture mappings
 * - Eliminates code duplication
 * - Makes adding new texture types trivial (just add one entry)
 * - Guarantees consistency across material and view synchronization
 */
constexpr std::array<TextureMapping, 11> g_textureMappings = {{
    {&MaterialAsset::albedoTexture,              MaterialTextureFlags::Albedo,              GLConfig::TextureSlots::Albedo},
    {&MaterialAsset::normalTexture,              MaterialTextureFlags::Normal,              GLConfig::TextureSlots::Normal},
    {&MaterialAsset::metallicRoughnessTexture,   MaterialTextureFlags::MetallicRoughness,   GLConfig::TextureSlots::MetallicRoughness},
    {&MaterialAsset::metallicTexture,            MaterialTextureFlags::Metallic,            GLConfig::TextureSlots::Metallic},
    {&MaterialAsset::roughnessTexture,           MaterialTextureFlags::Roughness,           GLConfig::TextureSlots::Roughness},
    {&MaterialAsset::aoTexture,                  MaterialTextureFlags::AO,                  GLConfig::TextureSlots::AO},
    {&MaterialAsset::aoMetallicRoughnessTexture, MaterialTextureFlags::AOMetallicRoughness, GLConfig::TextureSlots::MetallicRoughness},
    {&MaterialAsset::emissionTexture,            MaterialTextureFlags::Emission,            GLConfig::TextureSlots::Emission},
    {&MaterialAsset::heightTexture,              MaterialTextureFlags::Height,              GLConfig::TextureSlots::Height},
    {&MaterialAsset::clearcoatTexture,           MaterialTextureFlags::Clearcoat,           GLConfig::TextureSlots::Clearcoat},
    {&MaterialAsset::transmissionTexture,        MaterialTextureFlags::Transmission,        GLConfig::TextureSlots::Transmission},
}};

} // namespace Engine
