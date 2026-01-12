#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Centralized OpenGL backend configuration constants.
 * 
 * This header consolidates all OpenGL-specific constants used across the backend,
 * making it easier to maintain and modify the rendering pipeline configuration.
 * 
 * All constants are in a single location to:
 * - Prevent magic numbers scattered across the codebase
 * - Make the rendering pipeline configuration transparent
 * - Simplify modifications (e.g., changing texture slot assignments)
 * - Improve code maintainability and readability
 */
namespace GLConfig {

    /**
     * @brief Uniform Buffer Object (UBO) binding points.
     * 
     * These binding points connect shader uniform blocks to specific UBO indices.
     * Must match the layout(binding = N) qualifiers in GLSL shaders.
     */
    namespace UBOBindingPoints {
        constexpr uint32_t Material = 0;  ///< Material properties UBO
        constexpr uint32_t Lights   = 1;  ///< Lights array UBO
        constexpr uint32_t Camera   = 2;  ///< Camera/view matrices UBO (reserved for future use)
    }

    /**
     * @brief Texture unit slot assignments for PBR materials.
     * 
     * These slots define which texture unit each texture type is bound to.
     * Must match the sampler2D uniforms in GLSL shaders.
     * 
     * Note: Slots are reused for mutually exclusive textures (e.g., separate vs combined maps).
     */
    namespace TextureSlots {
        constexpr uint32_t Albedo               = 0;   ///< Base color/albedo texture
        constexpr uint32_t Normal               = 1;   ///< Normal map (tangent space)
        constexpr uint32_t MetallicRoughness    = 2;   ///< Combined metallic+roughness (shared with AO variant)
        constexpr uint32_t AO                   = 3;   ///< Ambient occlusion map
        constexpr uint32_t Emission             = 4;   ///< Emissive texture
        constexpr uint32_t Height               = 5;   ///< Height/displacement map
        constexpr uint32_t Clearcoat            = 6;   ///< Clearcoat intensity map
        constexpr uint32_t Transmission         = 7;   ///< Transmission map for transparent materials
        constexpr uint32_t Metallic             = 8;   ///< Separate metallic map (if not using combined)
        constexpr uint32_t Roughness            = 9;   ///< Separate roughness map (if not using combined)
        
        constexpr uint32_t Count = 10;  ///< Total number of texture slots used
    }

    /**
     * @brief Uniform name constants for shader uniforms.
     * 
     * Centralizes uniform names to prevent typos and make refactoring easier.
     */
    namespace UniformNames {
        // Camera/View uniforms
        constexpr const char* CameraPosition   = "u_cameraPosition";
        constexpr const char* View             = "u_view";
        constexpr const char* Projection       = "u_projection";
        constexpr const char* ViewProjection   = "u_viewProjection";
        constexpr const char* Model            = "u_model";

        // Texture sampler uniforms
        constexpr const char* AlbedoTexture              = "u_albedoTexture";
        constexpr const char* NormalTexture              = "u_normalTexture";
        constexpr const char* MetallicRoughnessTexture   = "u_metallicRoughnessTexture";
        constexpr const char* AOMetallicRoughnessTexture = "u_aoMetallicRoughnessTexture";
        constexpr const char* AOTexture                  = "u_aoTexture";
        constexpr const char* EmissionTexture            = "u_emissionTexture";
        constexpr const char* HeightTexture              = "u_heightTexture";
        constexpr const char* ClearcoatTexture           = "u_clearcoatTexture";
        constexpr const char* TransmissionTexture        = "u_transmissionTexture";
        constexpr const char* MetallicTexture            = "u_metallicTexture";
        constexpr const char* RoughnessTexture           = "u_roughnessTexture";

        // Debug uniforms
        constexpr const char* Color = "u_color";
    }

    /**
     * @brief Maximum limits for various resources.
     */
    namespace Limits {
        constexpr uint32_t MaxLights = 32;  ///< Maximum number of lights supported per frame
    }

} // namespace GLConfig

} // namespace Engine
