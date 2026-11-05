#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>


namespace Vkm::Engine {

/**
 * @brief Every binding point and texture unit the backend and its GLSL shaders share, once.
 *
 * Columns: GLBindings namespace, name, value, the #define shaderConstants gives every
 * stage, and a note. Material maps take the lowest units, then shadows, IBL and post; a pass that
 * reads no material map reuses the low ones. A fragment output is the scene target's color
 * attachment of that number: GLTarget indexes draw-buffer lists by location.
 *
 * Every texture unit up to highestTextureUnit() is spoken for: a new one shares a unit whose owner no
 * pass reading it binds, as the water's take the env cube's and GTAO's, or the numbering moves past
 * the per-stage count. An image unit, and an output of a target a pass owns, is that dispatch's or
 * that pass's own: numbered where it is bound, beside the one shader that reads it. The note is bare
 * macro tokens, so it holds no apostrophe: one opens a character literal, and the error lands in
 * whichever file includes this first.
 */
#define VKM_GL_BINDINGS(X)                                                                                                                                                \
    X(UBOBindingPoints,       MATERIAL,              0,  UBO_MATERIAL,                        Per-material PBR properties)                                                \
    X(UBOBindingPoints,       CAMERA,                2,  UBO_CAMERA,                          CameraBlock: matrices + position + viewport + planes)                       \
    X(UBOBindingPoints,       SHADOW,                3,  UBO_SHADOW,                          Shadow casters (cascades / spot / cube))                                    \
    X(UBOBindingPoints,       PROBES,                4,  UBO_PROBES,                          Reflection-probe boxes + layers (ProbeBlock))                               \
    X(UBOBindingPoints,       WAVES,                 5,  UBO_WAVES,                           The scene waves and their foam (WaveBlock))                                 \
    X(SSBOBindingPoints,      LIGHTS,                0,  SSBO_LIGHTS,                         Scene light list (grows past the UBO size limit))                           \
    X(SSBOBindingPoints,      CLUSTER_GRID,          1,  SSBO_CLUSTER_GRID,                   Per-cluster light lists)                                                    \
    X(SSBOBindingPoints,      PARTICLES,             2,  SSBO_PARTICLES,                      Billboard particle instances)                                               \
    X(SSBOBindingPoints,      IRRADIANCE_FILL,       3,  SSBO_IRRADIANCE_FILL,                How an irradiance bake repairs the probes it refused)                       \
    X(SSBOBindingPoints,      STRIP_POINTS,          4,  SSBO_STRIP_POINTS,                   Particle strip points: trails)                                              \
    X(SSBOBindingPoints,      SKIN_PALETTE,          5,  SSBO_SKIN_PALETTE,                   Every skinned item palette end to end)                                      \
    X(SSBOBindingPoints,      INSTANCE_SKIN_BASE,    6,  SSBO_INSTANCE_SKIN_BASE,             Per object: its first bone in SKIN_PALETTE)                                 \
    X(SSBOBindingPoints,      INSTANCE_FLAGS,        7,  SSBO_INSTANCE_FLAGS,                 Per object: how the eye draws it (shaders/opengl/include/viewmodel.glsl))   \
    X(SSBOBindingPoints,      INSTANCE_LIGHTMAP,     8,  SSBO_INSTANCE_LIGHTMAP,              Per object: lightmap page and place (shaders/opengl/include/lightmap.glsl)) \
    X(SSBOBindingPoints,      SWELL,                 9,  SSBO_SWELL,                          The swell grids and their foam - read by the wave vertices)                 \
    X(SSBOBindingPoints,      INSTANCE_MODELS,       10, SSBO_INSTANCE_MODELS,                Per object: its model matrix)                                               \
    X(SSBOBindingPoints,      INSTANCE_BOXES,        11, SSBO_INSTANCE_BOXES,                 Per object: its world box from the CPU cull (read by the GPU cull))         \
    X(SSBOBindingPoints,      CULL_VISIBLE,          12, SSBO_CULL_VISIBLE,                   Per object: whether the last cull saw it)                                   \
    X(SSBOBindingPoints,      CULL_COMMANDS,         13, SSBO_CULL_COMMANDS,                  The culled draws: a command list per set)                                   \
    X(SSBOBindingPoints,      CULL_INSTANCES,        14, SSBO_CULL_INSTANCES,                 The culled instances: an object list per set)                               \
    X(SSBOBindingPoints,      RELIGHT_LIGHTS,        15, SSBO_RELIGHT_LIGHTS,                 The lights reaching each irradiance probe cell (the relight))               \
    X(SSBOBindingPoints,      CULL_SOURCE,           16, SSBO_CULL_SOURCE,                    The opaque batch instances the GPU cull sorts)                              \
    X(SSBOBindingPoints,      EXPOSURE_HISTOGRAM,    17, SSBO_EXPOSURE_HISTOGRAM,             The log-luminance histogram of the frame (auto-exposure))                   \
    X(SSBOBindingPoints,      SWELL_SPECTRUM,        18, SSBO_SWELL_SPECTRUM,                 The swell cascades spectra the swell is drawn from)                         \
    X(SSBOBindingPoints,      DEBUG_LINES,           19, SSBO_DEBUG_LINES,                    The frame debug lines (shaders/opengl/overlay/debug_draw))                  \
    X(SSBOBindingPoints,      CULL_LAST,             20, SSBO_CULL_LAST,                      Per object: what the last cull saw (read while this one writes))            \
    X(SSBOBindingPoints,      LIGHTMAP_PROBE_SKY,    21, SSBO_LIGHTMAP_PROBE_SKY,             The sky each probe center sees (the lightmap bake))                         \
    X(SSBOBindingPoints,      WATER_BODIES,          22, SSBO_WATER_BODIES,                   Each Water: its shares of the waves and ripples and its foam)               \
    X(SSBOBindingPoints,      IRRADIANCE_VERDICT,    23, SSBO_IRRADIANCE_VERDICT,             Per irradiance probe: 1 when trusted - what a bake reads back)              \
    X(ShadowTextureSlots,     ATLAS_2D,              11, SHADOW_SLOT_ATLAS_2D,                Tiled 2D depth atlas (sampler2DShadow))                                     \
    X(ShadowTextureSlots,     CUBE,                  12, SHADOW_SLOT_CUBE,                    Point-light depth cubes: a layer each (samplerCubeArrayShadow))             \
    X(ShadowTextureSlots,     HELD,                  5,  SHADOW_SLOT_HELD,                    The sun cascades fixed casters: a layer each (sampler2DShadow))             \
    X(ShadowTextureSlots,     ATLAS_2D_RAW,          25, SHADOW_SLOT_ATLAS_2D_RAW,            The atlas uncompared (sampler2D): a unit holds one compare mode)            \
    X(IBLTextureSlots,        IRRADIANCE,            14, IBL_SLOT_IRRADIANCE,                 Diffuse irradiance cubemap (samplerCube))                                   \
    X(IBLTextureSlots,        PREFILTER,             15, IBL_SLOT_PREFILTER,                  Roughness-prefiltered specular cubemap (samplerCube))                       \
    X(IBLTextureSlots,        BRDF_LUT,              16, IBL_SLOT_BRDF_LUT,                   Split-sum BRDF/DFG LUT (sampler2D))                                         \
    X(IBLTextureSlots,        ENV_CUBE,              17, IBL_SLOT_ENV_CUBE,                   Sharp environment cubemap for the skybox (samplerCube))                     \
    X(WaterTextureSlots,      SHORE,                 17, WATER_SLOT_SHORE,                    Meters to what breaks the water - ENV_CUBE unit: the sky is drawn)          \
    X(WaterTextureSlots,      SHORE_WATER,           0,  WATER_SLOT_SHORE_WATER,              The water height the shore draw reads (u_water) - a free unit)              \
    X(WaterTextureSlots,      SHORE_UNDER,           0,  WATER_SLOT_SHORE_UNDER,              The shore drawn under the water - what its rows read (u_under))             \
    X(WaterTextureSlots,      SHORE_OVER,            1,  WATER_SLOT_SHORE_OVER,               And the one over it (u_over))                                               \
    X(WaterTextureSlots,      SHORE_ROWS,            0,  WATER_SLOT_SHORE_ROWS,               The shore rows its columns read (u_rows))                                   \
    X(WaterTextureSlots,      RIPPLES_ABOVE,         0,  WATER_SLOT_RIPPLES_ABOVE,            The ripple fields a level is read down from (u_fields))                     \
    X(CompositeTextureSlots,  SCENE,                 0,  COMPOSITE_SLOT_SCENE,                Final post-chain color (u_hdr))                                             \
    X(CompositeTextureSlots,  BLOOM,                 1,  COMPOSITE_SLOT_BLOOM,                Bloom mip 0 (u_bloom))                                                      \
    X(CompositeTextureSlots,  GRADING,               13, COMPOSITE_SLOT_GRADING,              The color grading table (sampler3D))                                        \
    X(LightmapTextureSlots,   SKY,                   13, LIGHTMAP_SLOT_SKY,                   Baked sky visibility - GRADING unit: unread by the forward pass)            \
    X(UpscaleTextureSlots,    SOURCE,                0,  UPSCALE_SLOT_SOURCE,                 The frame the upscale reads: the composite output then EASU output)         \
    X(BloomTextureSlots,      SOURCE,                0,  BLOOM_SLOT_SOURCE,                   Downsample/upsample source (u_src))                                         \
    X(BakeTextureSlots,       SOURCE,                0,  BAKE_SLOT_SOURCE,                    The equirect (u_equirect) or the env cube (u_envCube))                      \
    X(BakeTextureSlots,       TRANSMITTANCE,         1,  BAKE_SLOT_TRANSMITTANCE,             The atmosphere transmittance table (u_transmittance))                       \
    X(BakeTextureSlots,       MULTISCATTERING,       2,  BAKE_SLOT_MULTISCATTERING,           The atmosphere multiple-scattering table (u_multiScattering))               \
    X(SkyTextureSlots,        VIEW,                  0,  SKY_SLOT_VIEW,                       The sky-view table the skybox draws the sky from (u_skyViewLut))            \
    X(ParticleTextureSlots,   SPRITE,                0,  PARTICLE_SLOT_SPRITE,                What a run of particles shows (u_sprite) - a free material unit)            \
    X(ParticleTextureSlots,   NORMAL,                1,  PARTICLE_SLOT_NORMAL,                The normal map an alpha run lights its puffs by (u_normalMap))              \
    X(ParticleTextureSlots,   HALF_COLOR,            0,  PARTICLE_SLOT_HALF_COLOR,            The particles drawn at half resolution - SPRITE unit: unread then)          \
    X(ParticleTextureSlots,   HALF_DEPTH,            1,  PARTICLE_SLOT_HALF_DEPTH,            The depth they were drawn over - NORMAL unit: unread then)                  \
    X(OverlayTextureSlots,    UI_ATLAS,              0,  UI_SLOT_ATLAS,                       The UI font atlas or the empty one a run without text reads (u_tex))        \
    X(OverlayTextureSlots,    UI_IMAGE,              1,  UI_SLOT_IMAGE,                       The picture a UIImage run shows (u_image))                                  \
    X(OverlayTextureSlots,    SPLASH_LOGO,           0,  SPLASH_SLOT_LOGO,                    The splash logo (u_logo))                                                   \
    X(OverlayTextureSlots,    SPLASH_CAPTION,        1,  SPLASH_SLOT_CAPTION,                 The line at the foot of the splash (u_caption))                             \
    X(OverlayTextureSlots,    SPLASH_SHAPE,          2,  SPLASH_SLOT_SHAPE,                   How far inside the splash logo each texel is (u_shape))                     \
    X(OverlayTextureSlots,    SPLASH_GLOW,           3,  SPLASH_SLOT_GLOW,                    The splash logo blurred near and wide past its edge (u_glow))               \
    X(OverlayTextureSlots,    LOADING_WATER,         0,  LOADING_SLOT_WATER,                  The loading screen water: its heights and their speeds (u_water))           \
    X(IrradianceRelightSlots, SURFELS,               0,  RELIGHT_SLOT_SURFELS,                What the irradiance probes see as surfaces to light (u_surfels))            \
    X(LightmapBakeSlots,      POSITION,              0,  LIGHTMAP_BAKE_SLOT_POSITION,         Where each lightmap texel lies - w = 1 where a receiver does)               \
    X(LightmapBakeSlots,      NORMAL,                1,  LIGHTMAP_BAKE_SLOT_NORMAL,           The way each lightmap texel faces)                                          \
    X(LightmapBakeSlots,      DEPTH,                 2,  LIGHTMAP_BAKE_SLOT_DEPTH,            The maps of a fixed light - a layer a view (sampler2DArray))                \
    X(CullTextureSlots,       DEPTH,                 0,  CULL_SLOT_DEPTH,                     The scene depth the pyramid is built from (u_depth))                        \
    X(CullTextureSlots,       DEPTH_MS,              1,  CULL_SLOT_DEPTH_MS,                  The same while it is multisample (u_depthMS))                               \
    X(CullTextureSlots,       PYRAMID,               2,  CULL_SLOT_PYRAMID,                   The farthest-depth pyramid the cull tests boxes against)                    \
    X(ReflectionTextureSlots, WEIGHT,                0,  REFLECT_SLOT_WEIGHT,                 The forward pass reflection weight (rgb) + roughness (a))                   \
    X(ReflectionTextureSlots, ENV,                   1,  REFLECT_SLOT_ENV,                    That weight times the probe / sky radiance)                                 \
    X(ReflectionTextureSlots, TRACE,                 2,  REFLECT_SLOT_TRACE,                  Each pixel hit: uv + trust + the chain level its lobe spans)                \
    X(ReflectionTextureSlots, CHAIN,                 3,  REFLECT_SLOT_CHAIN,                  The lit scene as a filtered mip chain that a hit reads)                     \
    X(PostTextureSlots,       SCENE_COLOR,           18, POST_SLOT_SCENE_COLOR,               Scene color: refraction backdrop and a post pass source)                    \
    X(PostTextureSlots,       SCENE_DEPTH,           19, POST_SLOT_SCENE_DEPTH,               Scene depth texture)                                                        \
    X(PostTextureSlots,       SCENE_GBUFFER,         20, POST_SLOT_SCENE_GBUFFER,             Scene G-buffer: the oct view-normal)                                        \
    X(LightmapTextureSlots,   MASK,                  20, LIGHTMAP_SLOT_MASK,                  Baked shadows - SCENE_GBUFFER unit: unread by the forward pass)             \
    X(PostTextureSlots,       AO,                    21, POST_SLOT_AO,                        GTAO factor + octahedral bent normal)                                       \
    X(ProbeTextureSlots,      IRRADIANCE,            22, PROBE_SLOT_IRRADIANCE,               samplerCubeArray of every probe irradiance)                                 \
    X(ProbeTextureSlots,      PREFILTER,             23, PROBE_SLOT_PREFILTER,                samplerCubeArray of every probe prefilter)                                  \
    X(PostTextureSlots,       FOG_VOLUME,            24, POST_SLOT_FOG_VOLUME,                Integrated froxel fog (sampler3D) read by shaders/opengl/include/fog.glsl)  \
    X(IrradianceVolumeSlots,  SH0,                   26, IRRADIANCE_SLOT_SH0,                 Baked SH-L1 coefficient 0 (sampler3D))                                      \
    X(IrradianceVolumeSlots,  SH1,                   27, IRRADIANCE_SLOT_SH1,                 Baked SH-L1 coefficient 1 (sampler3D))                                      \
    X(IrradianceVolumeSlots,  SH2,                   28, IRRADIANCE_SLOT_SH2,                 Baked SH-L1 coefficient 2 (sampler3D))                                      \
    X(IrradianceVolumeSlots,  SH3,                   29, IRRADIANCE_SLOT_SH3,                 Baked SH-L1 coefficient 3 (sampler3D))                                      \
    X(PostTextureSlots,       AERIAL_PERSPECTIVE,    30, POST_SLOT_AERIAL_PERSPECTIVE,        The sky air in front of the view (sampler3D) read through fog.glsl)         \
    X(PostTextureSlots,       AO_DEPTH,              31, POST_SLOT_AO_DEPTH,                  GTAO linear-depth mip chain)                                                \
    X(WaterTextureSlots,      RIPPLES,               31, WATER_SLOT_RIPPLES,                  Ripple fields (sampler2DArray) - AO_DEPTH unit: GTAO is done)               \
    X(TextureSlots,           ALBEDO,                0,  MATERIAL_SLOT_ALBEDO,                Material map; the slot is also its bit in MaterialUBO.textureFlags)         \
    X(TextureSlots,           NORMAL,                1,  MATERIAL_SLOT_NORMAL,                Material map)                                                               \
    X(TextureSlots,           METALLIC_ROUGHNESS,    2,  MATERIAL_SLOT_METALLIC_ROUGHNESS,    Material map)                                                               \
    X(TextureSlots,           AO,                    3,  MATERIAL_SLOT_AO,                    Material map)                                                               \
    X(TextureSlots,           EMISSION,              4,  MATERIAL_SLOT_EMISSION,              Material map)                                                               \
    X(TextureSlots,           CLEARCOAT,             6,  MATERIAL_SLOT_CLEARCOAT,             Material map)                                                               \
    X(TextureSlots,           TRANSMISSION,          7,  MATERIAL_SLOT_TRANSMISSION,          Material map)                                                               \
    X(TextureSlots,           METALLIC,              8,  MATERIAL_SLOT_METALLIC,              Material map)                                                               \
    X(TextureSlots,           ROUGHNESS,             9,  MATERIAL_SLOT_ROUGHNESS,             Material map)                                                               \
    X(TextureSlots,           AO_METALLIC_ROUGHNESS, 10, MATERIAL_SLOT_AO_METALLIC_ROUGHNESS, Material map)                                                               \
    X(LayerTextureSlots,      ALBEDO,                6,  LAYER_SLOT_ALBEDO,                   A layered color array - CLEARCOAT unit: unread by LAYERED)                  \
    X(LayerTextureSlots,      NORMAL,                7,  LAYER_SLOT_NORMAL,                   Its normal array - TRANSMISSION unit: likewise)                             \
    X(LayerTextureSlots,      SURFACE,               8,  LAYER_SLOT_SURFACE,                  Its occlusion + roughness + metal array - METALLIC unit: likewise)          \
    X(LayerTextureSlots,      COPY_SOURCE,           0,  LAYER_SLOT_COPY_SOURCE,              The map copied into a layer of an array (u_source))                         \
    X(VertexAttributes,       POSITION,              0,  ATTR_POSITION,                       Mesh-pool stream)                                                           \
    X(VertexAttributes,       NORMAL,                1,  ATTR_NORMAL,                         Mesh-pool stream)                                                           \
    X(VertexAttributes,       UV,                    2,  ATTR_UV,                             Mesh-pool stream)                                                           \
    X(VertexAttributes,       TANGENT,               3,  ATTR_TANGENT,                        Mesh-pool stream)                                                           \
    X(VertexAttributes,       INSTANCE,              4,  ATTR_INSTANCE,                       The object an instance draws: its index into the per-object buffers)        \
    X(VertexAttributes,       LIGHTMAP_UV,           5,  ATTR_LIGHTMAP_UV,                    Mesh-pool lightmap stream; static layout alone)                             \
    X(VertexAttributes,       BONES,                 8,  ATTR_BONES,                          Mesh-pool skin stream)                                                      \
    X(VertexAttributes,       WEIGHTS,               9,  ATTR_WEIGHTS,                        Mesh-pool skin stream)                                                      \
    X(UIAttributes,           RECT,                  0,  UI_ATTR_RECT,                        A UI quad: its top-left and bottom-right corners in screen px)              \
    X(UIAttributes,           UV,                    1,  UI_ATTR_UV,                          Its texture coordinates at those corners)                                   \
    X(UIAttributes,           TOP,                   2,  UI_ATTR_TOP,                         The fill along its top edge)                                                \
    X(UIAttributes,           BOTTOM,                3,  UI_ATTR_BOTTOM,                      And along its bottom)                                                       \
    X(UIAttributes,           BORDER,                4,  UI_ATTR_BORDER,                      Its border color)                                                           \
    X(UIAttributes,           STYLE,                 5,  UI_ATTR_STYLE,                       Corner radius + border width + image)                                       \
    X(FragmentOutputs,        COLOR,                 0,  OUT_COLOR,                           The lit HDR color)                                                          \
    X(FragmentOutputs,        GBUFFER,               1,  OUT_GBUFFER,                         Oct view normal)                                                            \
    X(FragmentOutputs,        REFLECT_WEIGHT,        2,  OUT_REFLECT_WEIGHT,                  The environment reflection weight (rgb) + roughness (a))                    \
    X(FragmentOutputs,        REFLECT_ENV,           3,  OUT_REFLECT_ENV,                     That weight times the probe / sky radiance)                                 \
    X(ComputeGroups,          IMAGE,                 8,  GROUP_IMAGE,                         Edge of a square group over a 2D image or a froxel slice)                   \
    X(ComputeGroups,          CLUSTERS,              64, GROUP_CLUSTERS,                      Clusters culled per group)                                                  \
    X(ComputeGroups,          CULL,                  64, GROUP_CULL,                          Instances the GPU occlusion cull sorts per group)                           \
    X(ComputeGroups,          FILL,                  64, GROUP_FILL,                          Refused irradiance probes filled per group)                                 \
    X(ComputeGroups,          RIPPLE_FFT,            256, GROUP_RIPPLE_FFT,                   A butterfly each across a ripple field line: half its texels)               \
    X(ComputeGroups,          SWELL_FFT,             32, GROUP_SWELL_FFT,                     A butterfly each across a line of a swell cascade: half its texels)

/**
 * @brief The fixed contract between the OpenGL backend and its GLSL shaders.
 *
 * The shaders never repeat a number: shaderConstants writes each into every stage's
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
     * @brief The highest texture unit the table numbers.
     *
     * The OpenGL version vkmOpenGL needs (Vkm::OpenGL::MAJOR_VERSION) guarantees 16 a stage; the numbering relies on the 32
     * or more every targeted desktop driver reports, and GLBackend::init refuses a device with no
     * more than this one.
     *
     * @return The largest unit of a texture family: every family but the buffers', the attributes',
     *         the outputs' and the groups'.
     */
    constexpr uint32_t highestTextureUnit() {
        uint32_t highest = 0;
#define VKM_GL_BINDING_UNIT(group, name, value, glsl, doc)                                       \
        if (std::string_view(#group).find("Slots") != std::string_view::npos)                   \
            highest = std::max(highest, uint32_t{value});
        VKM_GL_BINDINGS(VKM_GL_BINDING_UNIT)
#undef VKM_GL_BINDING_UNIT
        return highest;
    }

    /**
     * @brief The highest storage-buffer binding point the table numbers.
     *
     * The OpenGL version vkmOpenGL needs (Vkm::OpenGL::MAJOR_VERSION) guarantees eight binding points;
     * GLBackend::init refuses a device with no more than this one.
     *
     * @return The largest SSBOBindingPoints value.
     */
    constexpr uint32_t highestSSBOBinding() {
        uint32_t highest = 0;
#define VKM_GL_BINDING_SSBO(group, name, value, glsl, doc) \
        if (std::string_view(#group) == "SSBOBindingPoints") highest = std::max(highest, uint32_t{value});
        VKM_GL_BINDINGS(VKM_GL_BINDING_SSBO)
#undef VKM_GL_BINDING_SSBO
        return highest;
    }

    // The boundaries between the families, checked: a unit goes wrong only by meeting another.
    static_assert(
        TextureSlots::AO_METALLIC_ROUGHNESS < ShadowTextureSlots::ATLAS_2D,
        "Material maps would overlap the shadow atlas slot"
    );
    static_assert(
        TextureSlots::EMISSION < ShadowTextureSlots::HELD
            && ShadowTextureSlots::HELD < TextureSlots::CLEARCOAT,
        "The sun's held layers take the one unit the material maps leave free"
    );
    static_assert(
        ShadowTextureSlots::CUBE > ShadowTextureSlots::ATLAS_2D,
        "The point-light cubes would overlap the 2D shadow atlas"
    );
    static_assert(
        ShadowTextureSlots::CUBE < IBLTextureSlots::IRRADIANCE,
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
        IrradianceVolumeSlots::SH3 < PostTextureSlots::AERIAL_PERSPECTIVE,
        "The irradiance volume would overlap the aerial-perspective volume"
    );
    static_assert(
        PostTextureSlots::AERIAL_PERSPECTIVE < PostTextureSlots::AO_DEPTH,
        "The aerial-perspective volume would overlap the GTAO depth chain"
    );

} // namespace GLBindings

} // namespace Vkm::Engine
