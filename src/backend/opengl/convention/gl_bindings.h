#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief The fixed contract between the OpenGL backend and its GLSL shaders.
 *
 * Which UBO binding point each uniform block uses, and which texture unit each
 * material map binds to. These numbers MUST match the `layout(std140, binding =
 * N)` blocks and the sampler bindings declared in the shaders.
 */
namespace GLBindings {

    // UBO binding points - match `layout(std140, binding = N)` in the shaders.
    namespace UBOBindingPoints {
        constexpr uint32_t Material = 0;  ///< Per-material PBR properties.
        constexpr uint32_t Lights   = 1;  ///< Scene light list.
        constexpr uint32_t Camera   = 2;  ///< Per-frame camera (viewProjection, position).
        constexpr uint32_t Shadow   = 3;  ///< Shadow casters (cascades / spot / cube).
    }

    // Texture units above the material maps (0-10), for the shadow pass outputs.
    // The cube samplers occupy ShadowCubeBase .. ShadowCubeBase+MAX_CUBE-1.
    namespace ShadowTextureSlots {
        constexpr uint32_t Atlas2D  = 11;  ///< Tiled 2D depth atlas (sampler2D).
        constexpr uint32_t CubeBase = 12;  ///< First point-light depth cube (samplerCube[]).
    }

    // Image-based lighting textures, above the shadow slots (11-13). Bound by
    // the forward pass (ambient) and the skybox pass (EnvCube).
    namespace IBLTextureSlots {
        constexpr uint32_t Irradiance = 14;  ///< Diffuse irradiance cubemap (samplerCube).
        constexpr uint32_t Prefilter  = 15;  ///< Roughness-prefiltered specular cubemap (samplerCube).
        constexpr uint32_t BrdfLUT    = 16;  ///< Split-sum BRDF/DFG LUT (sampler2D).
        constexpr uint32_t EnvCube    = 17;  ///< Sharp environment cubemap (skybox; samplerCube).
    }

    // Post-process inputs above the IBL slots.
    namespace PostTextureSlots {
        constexpr uint32_t SceneColor = 18;  ///< Copy of the opaque+sky scene (transmission refraction).
    }

    // Texture unit slots for material maps - match the sampler bindings in the
    // fragment shader. A material binds only the maps it actually has. The
    // slot number doubles as the map's bit position in MaterialUBO.textureFlags.
    namespace TextureSlots {
        constexpr uint32_t Albedo              = 0;
        constexpr uint32_t Normal              = 1;
        constexpr uint32_t MetallicRoughness   = 2;
        constexpr uint32_t AO                  = 3;
        constexpr uint32_t Emission            = 4;
        constexpr uint32_t Height              = 5;
        constexpr uint32_t Clearcoat           = 6;
        constexpr uint32_t Transmission        = 7;
        constexpr uint32_t Metallic            = 8;
        constexpr uint32_t Roughness           = 9;
        constexpr uint32_t AOMetallicRoughness = 10;
    }

    // Bits packed into MaterialUBO.textureFlags: which maps are bound, so the
    // shader knows which to sample. Bit position == the map's texture slot.
    // Must match the FLAG_* constants in shaders/forward.
    namespace MaterialTextureFlags {
        constexpr int Albedo              = 1 << TextureSlots::Albedo;
        constexpr int Normal              = 1 << TextureSlots::Normal;
        constexpr int MetallicRoughness   = 1 << TextureSlots::MetallicRoughness;
        constexpr int AO                  = 1 << TextureSlots::AO;
        constexpr int Emission            = 1 << TextureSlots::Emission;
        constexpr int Height              = 1 << TextureSlots::Height;
        constexpr int Clearcoat           = 1 << TextureSlots::Clearcoat;
        constexpr int Transmission        = 1 << TextureSlots::Transmission;
        constexpr int Metallic            = 1 << TextureSlots::Metallic;
        constexpr int Roughness           = 1 << TextureSlots::Roughness;
        constexpr int AOMetallicRoughness = 1 << TextureSlots::AOMetallicRoughness;
    }

} // namespace GLBindings

} // namespace Engine
