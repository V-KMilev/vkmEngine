#pragma once

#include <cstdint>

#include "core/engine_config.h"

namespace Vkm::Engine {

/**
 * @brief Every binding point and texture unit the backend and its GLSL shaders share, once.
 *
 * Columns: GLBindings namespace, name, value, the #define GLBackend::shaderConstants gives every
 * stage, and a note. Material maps take the lowest units, then shadows, IBL and post; a pass that
 * reads no material map reuses the low ones. A fragment output is the scene target's colour
 * attachment of that number: GLTarget indexes draw-buffer lists by location.
 */
#define VKM_GL_BINDINGS(X)                                                                                                                                         \
    X(UBOBindingPoints,       MATERIAL,              0,  UBO_MATERIAL,                        Per-material PBR properties)                                         \
    X(UBOBindingPoints,       CAMERA,                2,  UBO_CAMERA,                          CameraBlock: matrices + position + viewport + planes)                \
    X(UBOBindingPoints,       SHADOW,                3,  UBO_SHADOW,                          Shadow casters (cascades / spot / cube))                             \
    X(UBOBindingPoints,       PROBES,                4,  UBO_PROBES,                          Reflection-probe boxes + layers (ProbeBlock))                        \
    X(SSBOBindingPoints,      LIGHTS,                0,  SSBO_LIGHTS,                         Scene light list (grows past the UBO size limit))                    \
    X(SSBOBindingPoints,      CLUSTER_GRID,          1,  SSBO_CLUSTER_GRID,                   Per-cluster light lists)                                             \
    X(SSBOBindingPoints,      PARTICLES,             2,  SSBO_PARTICLES,                      Billboard particle instances)                                        \
    X(SSBOBindingPoints,      SKIN_PALETTE,          5,  SSBO_SKIN_PALETTE,                   Every skinned item palette end to end)                               \
    X(SSBOBindingPoints,      INSTANCE_SKIN_BASE,    6,  SSBO_INSTANCE_SKIN_BASE,             Per object: its first bone in SKIN_PALETTE)                          \
    X(SSBOBindingPoints,      INSTANCE_MODELS,       10, SSBO_INSTANCE_MODELS,                Per object: its model matrix)                                        \
    X(ShadowTextureSlots,     ATLAS_2D,              11, SHADOW_SLOT_ATLAS_2D,                Tiled 2D depth atlas (sampler2DShadow))                              \
    X(ShadowTextureSlots,     CUBE_BASE,             12, SHADOW_SLOT_CUBE_BASE,               First point-light depth cube (samplerCubeShadow[]))                  \
    X(ShadowTextureSlots,     ATLAS_2D_RAW,          25, SHADOW_SLOT_ATLAS_2D_RAW,            The atlas uncompared (sampler2D): a unit holds one comparison mode) \
    X(IBLTextureSlots,        IRRADIANCE,            14, IBL_SLOT_IRRADIANCE,                 Diffuse irradiance cubemap (samplerCube))                            \
    X(IBLTextureSlots,        PREFILTER,             15, IBL_SLOT_PREFILTER,                  Roughness-prefiltered specular cubemap (samplerCube))                \
    X(IBLTextureSlots,        BRDF_LUT,              16, IBL_SLOT_BRDF_LUT,                   Split-sum BRDF/DFG LUT (sampler2D))                                  \
    X(IBLTextureSlots,        ENV_CUBE,              17, IBL_SLOT_ENV_CUBE,                   Sharp environment cubemap for the skybox (samplerCube))              \
    X(CompositeTextureSlots,  SCENE,                 0,  COMPOSITE_SLOT_SCENE,                Final post-chain colour (u_hdr))                                     \
    X(CompositeTextureSlots,  BLOOM,                 1,  COMPOSITE_SLOT_BLOOM,                Bloom mip 0 (u_bloom))                                               \
    X(BloomTextureSlots,      SOURCE,                0,  BLOOM_SLOT_SOURCE,                   Downsample/upsample source (u_src))                                  \
    X(BakeTextureSlots,       SOURCE,                0,  BAKE_SLOT_SOURCE,                    The equirect (u_equirect) or the env cube (u_envCube))               \
    X(OverlayTextureSlots,    UI_ATLAS,              0,  UI_SLOT_ATLAS,                       The UI font atlas or the empty one a run without text reads (u_tex)) \
    X(OverlayTextureSlots,    UI_IMAGE,              1,  UI_SLOT_IMAGE,                       The picture a UIImage run shows (u_image))                           \
    X(OverlayTextureSlots,    SPLASH_LOGO,           0,  SPLASH_SLOT_LOGO,                    The splash logo (u_logo))                                            \
    X(IrradianceProjectSlots, PROBE,                 0,  PROJECT_SLOT_PROBE,                  The captured radiance cube (u_probe))                                \
    X(IrradianceProjectSlots, BACKFACE,              1,  PROJECT_SLOT_BACKFACE,               The backface mask captured beside it (u_backface))                   \
    X(ReflectionTextureSlots, WEIGHT,                0,  REFLECT_SLOT_WEIGHT,                 The forward pass reflection weight (rgb) + roughness (a))            \
    X(ReflectionTextureSlots, ENV,                   1,  REFLECT_SLOT_ENV,                    That weight times the probe / sky radiance)                          \
    X(ReflectionTextureSlots, TRACE,                 2,  REFLECT_SLOT_TRACE,                  Each pixel hit: uv + trust + the chain level its lobe spans)         \
    X(ReflectionTextureSlots, CHAIN,                 3,  REFLECT_SLOT_CHAIN,                  The lit scene as a filtered mip chain that a hit reads)              \
    X(PostTextureSlots,       SCENE_COLOR,           18, POST_SLOT_SCENE_COLOR,               Scene colour: refraction backdrop and a post pass source)            \
    X(PostTextureSlots,       SCENE_DEPTH,           19, POST_SLOT_SCENE_DEPTH,               Scene depth texture)                                                 \
    X(PostTextureSlots,       SCENE_GBUFFER,         20, POST_SLOT_SCENE_GBUFFER,             Scene G-buffer: oct view-normal + roughness + metalness)             \
    X(PostTextureSlots,       AO,                    21, POST_SLOT_AO,                        GTAO occlusion factor)                                               \
    X(ProbeTextureSlots,      IRRADIANCE,            22, PROBE_SLOT_IRRADIANCE,               samplerCubeArray of every probe irradiance)                          \
    X(ProbeTextureSlots,      PREFILTER,             23, PROBE_SLOT_PREFILTER,                samplerCubeArray of every probe prefilter)                           \
    X(PostTextureSlots,       FOG_VOLUME,            24, POST_SLOT_FOG_VOLUME,                Integrated froxel fog (sampler3D) read through shaders/fog.glsl)     \
    X(IrradianceVolumeSlots,  SH0,                   26, IRRADIANCE_SLOT_SH0,                 Baked SH-L1 coefficient 0 (sampler3D))                               \
    X(IrradianceVolumeSlots,  SH1,                   27, IRRADIANCE_SLOT_SH1,                 Baked SH-L1 coefficient 1 (sampler3D))                               \
    X(IrradianceVolumeSlots,  SH2,                   28, IRRADIANCE_SLOT_SH2,                 Baked SH-L1 coefficient 2 (sampler3D))                               \
    X(IrradianceVolumeSlots,  SH3,                   29, IRRADIANCE_SLOT_SH3,                 Baked SH-L1 coefficient 3 (sampler3D))                               \
    X(PostTextureSlots,       AO_DEPTH,              31, POST_SLOT_AO_DEPTH,                  GTAO linear-depth mip chain)                                         \
    X(TextureSlots,           ALBEDO,                0,  MATERIAL_SLOT_ALBEDO,                Material map; the slot is also its bit in MaterialUBO.textureFlags)  \
    X(TextureSlots,           NORMAL,                1,  MATERIAL_SLOT_NORMAL,                Material map)                                                        \
    X(TextureSlots,           METALLIC_ROUGHNESS,    2,  MATERIAL_SLOT_METALLIC_ROUGHNESS,    Material map)                                                        \
    X(TextureSlots,           AO,                    3,  MATERIAL_SLOT_AO,                    Material map)                                                        \
    X(TextureSlots,           EMISSION,              4,  MATERIAL_SLOT_EMISSION,              Material map)                                                        \
    X(TextureSlots,           HEIGHT,                5,  MATERIAL_SLOT_HEIGHT,                Material map)                                                        \
    X(TextureSlots,           CLEARCOAT,             6,  MATERIAL_SLOT_CLEARCOAT,             Material map)                                                        \
    X(TextureSlots,           TRANSMISSION,          7,  MATERIAL_SLOT_TRANSMISSION,          Material map)                                                        \
    X(TextureSlots,           METALLIC,              8,  MATERIAL_SLOT_METALLIC,              Material map)                                                        \
    X(TextureSlots,           ROUGHNESS,             9,  MATERIAL_SLOT_ROUGHNESS,             Material map)                                                        \
    X(TextureSlots,           AO_METALLIC_ROUGHNESS, 10, MATERIAL_SLOT_AO_METALLIC_ROUGHNESS, Material map)                                                        \
    X(VertexAttributes,       POSITION,              0,  ATTR_POSITION,                       Mesh-pool stream)                                                    \
    X(VertexAttributes,       NORMAL,                1,  ATTR_NORMAL,                         Mesh-pool stream)                                                    \
    X(VertexAttributes,       UV,                    2,  ATTR_UV,                             Mesh-pool stream)                                                    \
    X(VertexAttributes,       TANGENT,               3,  ATTR_TANGENT,                        Mesh-pool stream)                                                    \
    X(VertexAttributes,       INSTANCE,              4,  ATTR_INSTANCE,                       The object an instance draws: its index into the per-object buffers) \
    X(VertexAttributes,       BONES,                 8,  ATTR_BONES,                          Mesh-pool skin stream)                                               \
    X(VertexAttributes,       WEIGHTS,               9,  ATTR_WEIGHTS,                        Mesh-pool skin stream)                                               \
    X(FragmentOutputs,        COLOR,                 0,  OUT_COLOR,                           The lit HDR colour)                                                  \
    X(FragmentOutputs,        GBUFFER,               1,  OUT_GBUFFER,                         Oct view normal + authored roughness + metalness)                    \
    X(FragmentOutputs,        REFLECT_WEIGHT,        2,  OUT_REFLECT_WEIGHT,                  The environment reflection weight (rgb) + roughness (a))             \
    X(FragmentOutputs,        REFLECT_ENV,           3,  OUT_REFLECT_ENV,                     That weight times the probe / sky radiance)                          \
    X(ComputeGroups,          IMAGE,                 8,  GROUP_IMAGE,                         Edge of a square group over a 2D image or a froxel slice)            \
    X(ComputeGroups,          CLUSTERS,              64, GROUP_CLUSTERS,                      Clusters culled per group)

/**
 * @brief The fixed contract between the OpenGL backend and its GLSL shaders.
 *
 * The shaders never repeat a number: GLBackend::shaderConstants writes each into every stage's
 * prelude, a binding point or unit as its VKM_GL_BINDINGS #define, a capacity as a const.
 */
namespace GLBindings {

#define VKM_GL_BINDING_CONSTANT(group, name, value, glsl, doc) \
    namespace group { constexpr uint32_t name = value; }
    VKM_GL_BINDINGS(VKM_GL_BINDING_CONSTANT)
#undef VKM_GL_BINDING_CONSTANT

    namespace ComputeGroups {
        /**
         * @brief How many groups of @p size cover @p extent invocations.
         *
         * @param extent Invocations wanted along one axis.
         * @param size   The group's size along it.
         * @return Groups to dispatch; the shader discards the overhang.
         */
        constexpr uint32_t covering(uint32_t extent, uint32_t size) {
            return (extent + size - 1) / size;
        }
    } // namespace ComputeGroups

    // Two samplers hold every probe, a layer each, so the count is bounded by layers and the
    // per-fragment loop, not by texture units.
    namespace ProbeTextureSlots {
        constexpr uint32_t MAX_PROBES = 32;  ///< Probe-array capacity + per-fragment loop cap.
    } // namespace ProbeTextureSlots

    /**
     * @brief Highest texture unit this backend binds.
     *
     * GL 4.3 guarantees 16 per stage; the numbering relies on the at least 32 every targeted
     * desktop driver reports. GLBackend::init refuses a device with fewer.
     */
    constexpr uint32_t MAX_TEXTURE_UNIT = 31;

    // The boundaries between the families, checked: a unit goes wrong only by meeting another.
    static_assert(
        TextureSlots::AO_METALLIC_ROUGHNESS < ShadowTextureSlots::ATLAS_2D,
        "Material maps would overlap the shadow atlas slot"
    );
    static_assert(
        ShadowTextureSlots::CUBE_BASE > ShadowTextureSlots::ATLAS_2D,
        "The point-light cubes would overlap the 2D shadow atlas"
    );
    static_assert(
        ShadowTextureSlots::CUBE_BASE + Config::MAX_SHADOW_CASTERS_CUBE <= IBLTextureSlots::IRRADIANCE,
        "The point-light cubes would overlap the IBL texture slots"
    );
    static_assert(
        IBLTextureSlots::ENV_CUBE < PostTextureSlots::SCENE_COLOR,
        "The IBL set would overlap the post-process inputs"
    );
    static_assert(
        PostTextureSlots::AO < ProbeTextureSlots::IRRADIANCE,
        "The post inputs would overlap the reflection-probe arrays"
    );
    static_assert(
        ProbeTextureSlots::PREFILTER < PostTextureSlots::FOG_VOLUME,
        "The probe arrays would overlap the fog volume"
    );
    static_assert(
        PostTextureSlots::FOG_VOLUME < ShadowTextureSlots::ATLAS_2D_RAW,
        "The fog volume would overlap the raw shadow atlas"
    );
    static_assert(
        ShadowTextureSlots::ATLAS_2D_RAW < IrradianceVolumeSlots::SH0,
        "The raw shadow atlas would overlap the irradiance volume"
    );
    static_assert(
        IrradianceVolumeSlots::SH3 < PostTextureSlots::AO_DEPTH,
        "The irradiance volume would overlap the GTAO depth chain"
    );
    static_assert(
        PostTextureSlots::AO_DEPTH <= MAX_TEXTURE_UNIT,
        "A texture unit past what the backend assumes the driver has"
    );

} // namespace GLBindings

} // namespace Vkm::Engine
