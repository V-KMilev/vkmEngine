#pragma once

#include <cstdint>

#include "core/engine_config.h"

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
        constexpr uint32_t MATERIAL = 0;  ///< Per-material PBR properties.
        constexpr uint32_t CAMERA   = 2;  ///< Per-frame camera (viewProjection, position).
        constexpr uint32_t SHADOW   = 3;  ///< Shadow casters (cascades / spot / cube).
        constexpr uint32_t PROBES   = 4;  ///< Reflection-probe boxes + layers (ProbeBlock).
    } // namespace UBOBindingPoints

    // SSBO binding points - match `layout(std430, binding = N)` in the shaders.
    // A separate namespace from the UBO points (GL binds them independently).
    namespace SSBOBindingPoints {
        constexpr uint32_t LIGHTS       = 0;  ///< Scene light list (grows past the UBO size limit).
        constexpr uint32_t CLUSTER_GRID = 1;  ///< Per-cluster light lists (written by the cull compute, read by forward).
        constexpr uint32_t PARTICLES    = 2;  ///< Billboard particle instances, indexed by the particle vertex stage.

        // The frame's bone palettes, and where each instance starts in them.
        // The base is indexed by the instance slot, not by draw position: the
        // GPU cull compacts by rewriting that slot, so a divisor-1 attribute
        // would hand a culled batch another character's bones.
        constexpr uint32_t SKIN_PALETTE       = 5;  ///< Every skinned item's palette, end to end.
        constexpr uint32_t INSTANCE_SKIN_BASE = 6;  ///< Per-instance first bone in SKIN_PALETTE, batch order.

        // The cull's working set (3-9) and the per-instance transforms the camera
        // batch reads (10-12). Instance data is storage rather than attributes so
        // the cull picks by index; the shadow pass takes matrices as attributes.
        constexpr uint32_t CULL_BOUNDS    = 3;
        constexpr uint32_t CULL_RUN_INDEX = 4;
        constexpr uint32_t CULL_VISIBLE   = 7;   // 8 is free: the cull reads bounds and writes indices, never matrices
        constexpr uint32_t CULL_COMMANDS  = 9;

        constexpr uint32_t INSTANCE_MODELS  = 10;  ///< Per-instance model matrices, batch order.
        // 11 is free: the index buffer reaches the vertex stage as an attribute.
        constexpr uint32_t INSTANCE_NORMALS = 12;  ///< Per-instance normal matrices, batch order.
    } // namespace SSBOBindingPoints

    // Texture units above the material maps (0-10), for the shadow pass outputs.
    // The cube samplers occupy CUBE_BASE .. CUBE_BASE+MAX_CUBE-1.
    namespace ShadowTextureSlots {
        constexpr uint32_t ATLAS_2D     = 11;  ///< Tiled 2D depth atlas (sampler2DShadow).
        // The same atlas read as a depth image rather than as a shadow map, on a
        // unit of its own: a comparison lives on the sampler bound to a unit, so
        // one unit serving both readings lets the last pass decide for the next.
        constexpr uint32_t ATLAS_2D_RAW = 25;  ///< Tiled 2D depth atlas, no comparison (sampler2D).
        constexpr uint32_t CUBE_BASE    = 12;  ///< First point-light depth cube (samplerCube[]).

        // The cubes run CUBE_BASE .. CUBE_BASE+MAX-1 and the IBL set starts at 14,
        // so raising MAX_SHADOW_CASTERS_CUBE past two silently overwrites the
        // irradiance cube rather than failing. Say so at build time instead.
        static_assert(CUBE_BASE + Config::MAX_SHADOW_CASTERS_CUBE <= 14,
                      "Point-light shadow cubes would overlap the IBL texture slots");
    } // namespace ShadowTextureSlots

    // Image-based lighting textures, above the shadow slots (11-13). Bound by
    // the forward pass (ambient) and the skybox pass (ENV_CUBE).
    namespace IBLTextureSlots {
        constexpr uint32_t IRRADIANCE = 14;  ///< Diffuse irradiance cubemap (samplerCube).
        constexpr uint32_t PREFILTER  = 15;  ///< Roughness-prefiltered specular cubemap (samplerCube).
        constexpr uint32_t BRDF_LUT   = 16;  ///< Split-sum BRDF/DFG LUT (sampler2D).
        constexpr uint32_t ENV_CUBE   = 17;  ///< Sharp environment cubemap (skybox; samplerCube).
    } // namespace IBLTextureSlots

    // The low slots the composite + bloom shaders sample directly (their
    // samplers default to binding 0/1).
    namespace CompositeTextureSlots {
        constexpr uint32_t SCENE = 0;  ///< Final post-chain colour (u_hdr).
        constexpr uint32_t BLOOM = 1;  ///< Bloom mip 0 (u_bloom).
    } // namespace CompositeTextureSlots
    namespace BloomTextureSlots {
        constexpr uint32_t SOURCE = 0;  ///< Downsample/upsample source (u_src).
    } // namespace BloomTextureSlots

    // Post-process inputs above the IBL slots.
    namespace PostTextureSlots {
        constexpr uint32_t SCENE_COLOR   = 18;  ///< Scene colour (refraction + post-pass source: fog, DoF, decals).
        constexpr uint32_t SCENE_DEPTH   = 19;  ///< Scene depth texture (GTAO / decals / fog / DoF).
        constexpr uint32_t SCENE_GBUFFER = 20;  ///< Scene G-buffer: oct view-normal + roughness + metalness (GTAO / decals).
        constexpr uint32_t SSAO          = 21;  ///< GTAO occlusion factor, sampled by the forward pass.
        constexpr uint32_t FOG_VOLUME    = 24;  ///< Integrated froxel fog (sampler3D), sampled by the fog-apply pass.
        constexpr uint32_t HI_Z          = 30;  ///< Hierarchical depth pyramid: reduced by the HiZ pass, tested by the occlusion cull.
    } // namespace PostTextureSlots

    // Baked irradiance volume: SH-L1 coefficients, one sampler3D each.
    namespace IrradianceVolumeSlots {
        constexpr uint32_t SH0 = 26;
        constexpr uint32_t SH1 = 27;
        constexpr uint32_t SH2 = 28;
        constexpr uint32_t SH3 = 29;
    } // namespace IrradianceVolumeSlots

    // Reflection-probe cube-map arrays, above the post slots. Two samplers hold
    // every probe (layer = probe index), so the count is bounded by layers + the
    // per-fragment loop, not texture units. MAX_PROBES is the array capacity and
    // must match MAX_PROBES in shaders/forward/pbr.
    namespace ProbeTextureSlots {
        constexpr uint32_t MAX_PROBES = 32;  ///< Probe-array capacity + per-fragment loop cap.
        constexpr uint32_t IRRADIANCE = 22;  ///< samplerCubeArray (all probes' irradiance).
        constexpr uint32_t PREFILTER  = 23;  ///< samplerCubeArray (all probes' prefilter).
    } // namespace ProbeTextureSlots

    // Texture unit slots for material maps - match the sampler bindings in the
    // fragment shader. A material binds only the maps it actually has. The
    // slot number doubles as the map's bit position in MaterialUBO.textureFlags.
    namespace TextureSlots {
        constexpr uint32_t ALBEDO                = 0;
        constexpr uint32_t NORMAL                = 1;
        constexpr uint32_t METALLIC_ROUGHNESS    = 2;
        constexpr uint32_t AO                    = 3;
        constexpr uint32_t EMISSION              = 4;
        constexpr uint32_t HEIGHT                = 5;
        constexpr uint32_t CLEARCOAT             = 6;
        constexpr uint32_t TRANSMISSION          = 7;
        constexpr uint32_t METALLIC              = 8;
        constexpr uint32_t ROUGHNESS             = 9;
        constexpr uint32_t AO_METALLIC_ROUGHNESS = 10;
    } // namespace TextureSlots

    // Bits packed into MaterialUBO.textureFlags: which maps are bound, so the
    // shader knows which to sample. Bit position == the map's texture slot.
    // Must match the TEX_* constants in shaders/forward/pbr.
    namespace MaterialTextureFlags {
        constexpr int ALBEDO                = 1 << TextureSlots::ALBEDO;
        constexpr int NORMAL                = 1 << TextureSlots::NORMAL;
        constexpr int METALLIC_ROUGHNESS    = 1 << TextureSlots::METALLIC_ROUGHNESS;
        constexpr int AO                    = 1 << TextureSlots::AO;
        constexpr int EMISSION              = 1 << TextureSlots::EMISSION;
        constexpr int HEIGHT                = 1 << TextureSlots::HEIGHT;
        constexpr int CLEARCOAT             = 1 << TextureSlots::CLEARCOAT;
        constexpr int TRANSMISSION          = 1 << TextureSlots::TRANSMISSION;
        constexpr int METALLIC              = 1 << TextureSlots::METALLIC;
        constexpr int ROUGHNESS             = 1 << TextureSlots::ROUGHNESS;
        constexpr int AO_METALLIC_ROUGHNESS = 1 << TextureSlots::AO_METALLIC_ROUGHNESS;
    } // namespace MaterialTextureFlags

    // The boundaries between the families above, checked rather than trusted:
    // every number here is a unit or a binding point, and the only way one goes
    // wrong is by meeting another. A comment that they do not overlap cannot fail.
    namespace {
        /**
         * @brief Texture units this backend assumes exist.
         *
         * GL 4.3 guarantees 16 per stage; every desktop driver it targets reports
         * at least 32, which the numbering above already relies on.
         */
        constexpr uint32_t MAX_TEXTURE_UNIT = 31;

        static_assert(TextureSlots::AO_METALLIC_ROUGHNESS < ShadowTextureSlots::ATLAS_2D,
                      "Material maps would overlap the shadow atlas slot");
        static_assert(ShadowTextureSlots::CUBE_BASE > ShadowTextureSlots::ATLAS_2D,
                      "The point-light cubes would overlap the 2D shadow atlas");
        static_assert(IBLTextureSlots::ENV_CUBE < PostTextureSlots::SCENE_COLOR,
                      "The IBL set would overlap the post-process inputs");
        static_assert(PostTextureSlots::SSAO < ProbeTextureSlots::IRRADIANCE,
                      "The post inputs would overlap the reflection-probe arrays");
        static_assert(ProbeTextureSlots::PREFILTER < PostTextureSlots::FOG_VOLUME,
                      "The probe arrays would overlap the fog volume");
        static_assert(PostTextureSlots::FOG_VOLUME < ShadowTextureSlots::ATLAS_2D_RAW,
                      "The fog volume would overlap the raw shadow atlas");
        static_assert(ShadowTextureSlots::ATLAS_2D_RAW < IrradianceVolumeSlots::SH0,
                      "The raw shadow atlas would overlap the irradiance volume");
        static_assert(IrradianceVolumeSlots::SH3 < PostTextureSlots::HI_Z,
                      "The irradiance volume would overlap the Hi-Z pyramid");
        static_assert(PostTextureSlots::HI_Z <= MAX_TEXTURE_UNIT,
                      "A texture unit past what the backend assumes the driver has");
    } // namespace

} // namespace GLBindings

} // namespace Vkm::Engine
