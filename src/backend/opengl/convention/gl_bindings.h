#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief The fixed contract between the OpenGL backend and its GLSL shaders.
 *
 * Which UBO binding point each uniform block uses, and which texture unit each
 * material map binds to. These numbers MUST match the `layout(std140, binding =
 * N)` blocks and the sampler bindings declared in the shaders.
 */
namespace GLBindings {

    // UBO binding points - match `layout(std140, binding = N)` in the shaders.
    // (Binding 1 is free: the light list moved to an SSBO, below.)
    namespace UBOBindingPoints {
        constexpr uint32_t Material = 0;  ///< Per-material PBR properties.
        constexpr uint32_t Camera   = 2;  ///< Per-frame camera (viewProjection, position).
        constexpr uint32_t Shadow   = 3;  ///< Shadow casters (cascades / spot / cube).
        constexpr uint32_t Probes   = 4;  ///< Reflection-probe boxes + layers (ProbeBlock).
    }

    // SSBO binding points - match `layout(std430, binding = N)` in the shaders.
    // A separate namespace from the UBO points (GL binds them independently).
    namespace SSBOBindingPoints {
        constexpr uint32_t Lights      = 0;  ///< Scene light list (grows past the UBO size limit).
        constexpr uint32_t ClusterGrid = 1;  ///< Per-cluster light lists (written by the cull compute, read by forward).
        constexpr uint32_t Particles   = 2;  ///< Billboard particle instances, indexed by the particle vertex stage.

        // The GPU occlusion cull's working set (3-9) and the per-instance
        // transforms the camera batch's vertex stages read (10-12). Instance
        // data lives in storage rather than vertex attributes so the cull can
        // pick instances by writing an index instead of copying their matrices;
        // see shaders/_common/instancing.glsl. The shadow pass is the deliberate
        // exception and takes its matrices as attributes.
        constexpr uint32_t CullBounds     = 3;
        constexpr uint32_t CullRunIndex   = 4;
        constexpr uint32_t CullVisible    = 7;   // 5, 6 and 8 are free: the cull reads bounds and writes indices, never matrices
        constexpr uint32_t CullCommands   = 9;

        constexpr uint32_t InstanceModels  = 10;  ///< Per-instance model matrices, batch order.
        // 11 is free: the index buffer reaches the vertex stage as an attribute.
        constexpr uint32_t InstanceNormals = 12;  ///< Per-instance normal matrices, batch order.
    }

    // Texture units above the material maps (0-10), for the shadow pass outputs.
    // The cube samplers occupy CubeBase .. CubeBase+MAX_CUBE-1.
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

    // The low slots the composite + bloom shaders sample directly (their
    // samplers default to binding 0/1).
    namespace CompositeTextureSlots {
        constexpr uint32_t Scene = 0;  ///< Final post-chain colour (u_hdr).
        constexpr uint32_t Bloom = 1;  ///< Bloom mip 0 (u_bloom).
    }
    namespace BloomTextureSlots {
        constexpr uint32_t Source = 0;  ///< Downsample/upsample source (u_src).
    }

    // Post-process inputs above the IBL slots.
    namespace PostTextureSlots {
        constexpr uint32_t SceneColor = 18;  ///< Scene colour (refraction + post-pass source: fog, DoF, decals).
        constexpr uint32_t SceneDepth = 19;  ///< Scene depth texture (GTAO / decals / fog / DoF).
        constexpr uint32_t SceneGBuffer = 20; ///< Scene G-buffer: oct view-normal + roughness + metalness (GTAO / decals).
        constexpr uint32_t SSAO       = 21;  ///< GTAO occlusion factor, sampled by the forward pass.
        constexpr uint32_t FogVolume  = 24;  ///< Integrated froxel fog (sampler3D), sampled by the fog-apply pass.
        constexpr uint32_t HiZ        = 30;  ///< Hierarchical depth pyramid: reduced by the HiZ pass, tested by the occlusion cull.
    }

    // Baked irradiance volume: SH-L1 coefficients, one sampler3D each.
    namespace IrradianceVolumeSlots {
        constexpr uint32_t SH0 = 26;
        constexpr uint32_t SH1 = 27;
        constexpr uint32_t SH2 = 28;
        constexpr uint32_t SH3 = 29;
    }

    // Reflection-probe cube-map arrays, above the post slots. Two samplers hold
    // every probe (layer = probe index), so the count is bounded by layers + the
    // per-fragment loop, not texture units. MAX_PROBES is the array capacity and
    // must match MAX_PROBES in shaders/forward/pbr.
    namespace ProbeTextureSlots {
        constexpr uint32_t MAX_PROBES = 32;  ///< Probe-array capacity + per-fragment loop cap.
        constexpr uint32_t Irradiance = 22;  ///< samplerCubeArray (all probes' irradiance).
        constexpr uint32_t Prefilter  = 23;  ///< samplerCubeArray (all probes' prefilter).
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
    // Must match the TEX_* constants in shaders/forward/pbr.
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

} // namespace Vkm::Engine
