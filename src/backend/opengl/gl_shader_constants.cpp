#include "gl_shader_constants.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "asset/gl_layer_maps.h"
#include "frame/draw/gl_draw_list.h"
#include "frame/draw/gl_occlusion_cull.h"
#include "frame/gl_object_buffer.h"
#include "frame/water/gl_waves.h"
#include "offline/gl_irradiance_baker.h"
#include "offline/gl_lightmap_baker.h"
#include "pass/light/gl_reflection_pass.h"
#include "pass/scene/gl_particle_pass.h"
#include "storage/light/gl_atmosphere.h"
#include "storage/light/gl_cluster_grid.h"
#include "storage/light/gl_ibl.h"
#include "storage/light/gl_probe_array.h"
#include "core/render_mode.h"
#include "debug/draw/debug_line.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/light.h"
#include "ecs/settings/night_sky_settings.h"
#include "ecs/settings/wave_settings.h"
#include "ecs/water/swell.h"
#include "ecs/water/wave_spectrum.h"
#include "ecs/water/wave_surface.h"
#include "io/project/render_settings.h"
#include "resource/asset/material_asset.h"
#include "system/render/exposure.h"
#include "system/render/render_limits.h"
#include "system/sky/atmosphere.h"
#include "core/frame/splash_frame.h"
#include "core/frame/ui_draw_data.h"

namespace Vkm::Engine {

std::string shaderConstants() {
    using CullSet = GLOcclusionCull::Set;
    std::ostringstream out;

    // Scientific, so each value is a full-precision float literal, not an int or six digits.
    std::ostringstream num;
    num << std::scientific << std::setprecision(8);
    const auto number = [&num](float value) {
        num.str({});
        num << value;
        return num.str();
    };

    out << "const int   MAX_LIGHTS              = " << Config::MAX_LIGHTS              << ";\n"
        << "const int   MAX_SHADOW_CASTERS_2D   = " << Config::MAX_SHADOW_CASTERS_2D   << ";\n"
        << "const int   MAX_SHADOW_CASTERS_CUBE = " << Config::MAX_SHADOW_CASTERS_CUBE << ";\n"
        << "const int   NUM_CASCADES            = " << Config::NUM_CASCADES            << ";\n"
        << "const int   CLUSTER_X               = " << Config::CLUSTER_X               << ";\n"
        << "const int   CLUSTER_Y               = " << Config::CLUSTER_Y               << ";\n"
        << "const int   CLUSTER_Z               = " << Config::CLUSTER_Z               << ";\n"
        << "const int   MAX_LIGHTS_PER_CLUSTER  = " << Config::MAX_LIGHTS_PER_CLUSTER  << ";\n"
        << "const int   NUM_CLUSTERS            = " << GLClusterGrid::NUM_CLUSTERS     << ";\n"
        << "const int   MAX_WAVE_SURGES         = " << WaveSettings::MAX_SURGES << ";\n"
        << "const int   SWELL_FIELD             = " << SWELL_FIELD << ";\n"
        << "const int   MAX_SWELL_CASCADES      = " << MAX_SWELL_CASCADES << ";\n"
        << "const int   RIPPLE_FIELDS           = " << RIPPLE_FIELDS << ";\n"
        << "const int   WAVE_INVERSE_STEPS      = " << WaveSurface::INVERSE_STEPS << ";\n"
        << "const float WAVE_GRAVITY            = " << number(WaveSpectrum::GRAVITY) << ";\n"
        << "const float SURGE_WHOLE             = " << number(WaveSurface::SURGE_WHOLE) << ";\n"
        << "const float JONSWAP_GAMMA           = " << number(JONSWAP_GAMMA) << ";\n"
        << "const float JONSWAP_SIGMA_BELOW     = " << number(JONSWAP_SIGMA_BELOW) << ";\n"
        << "const float JONSWAP_SIGMA_ABOVE     = " << number(JONSWAP_SIGMA_ABOVE) << ";\n"
        << "const int   RIPPLE_FIELD            = " << RIPPLE_FIELD_SIZE << ";\n"
        << "const int   EXPOSURE_BINS           = " << Exposure::BINS << ";\n"
        << "const int   EXPOSURE_STRIDE         = " << Exposure::STRIDE << ";\n"
        << "const float EXPOSURE_MIN_LOG        = " << number(Exposure::MIN_LOG) << ";\n"
        << "const float EXPOSURE_MAX_LOG        = " << number(Exposure::MAX_LOG) << ";\n"
        << "const int   MAX_PROBES              = " << GLBindings::ProbeTextureSlots::MAX_PROBES << ";\n"
        // A LOD index, so one less than the mip count each cube carries.
        << "const float MAX_REFLECTION_LOD      = " << (GLIBL::PREFILTER_MIPS - 1)   << ".0;\n"
        << "const float MAX_PROBE_LOD           = " << (GLProbeArray::PREFILTER_MIPS - 1) << ".0;\n"
        << "const float UI_TEXT_MARK            = " << number(UI_TEXT_MARK) << ";\n"
        << "const float IDENT_SECONDS           = " << number(IDENT_SECONDS) << ";\n"
        << "const float PARTICLE_BEND_REACH     = " << number(GLParticlePass::BEND_REACH) << ";\n"
        << "const float SSR_MAX_ROUGHNESS       = " << number(GLReflectionPass::MAX_ROUGHNESS) << ";\n"
        << "const uint  INSTANCE_FLAG_VIEWMODEL = " << INSTANCE_FLAG_VIEWMODEL << "u;\n"
        << "const uint  INSTANCE_WATER_SHIFT    = " << INSTANCE_WATER_SHIFT << "u;\n"
        << "const uint  INSTANCE_OBJECT_BITS    = " << INSTANCE_OBJECT_BITS << "u;\n"
        << "const uint  INSTANCE_OBJECT_MASK    = " << INSTANCE_OBJECT_MASK << "u;\n"
        << "const uint  INSTANCE_FADE_STEPS     = " << INSTANCE_FADE_STEPS << "u;\n"
        << "const uint  INSTANCE_FADES_IN       = " << INSTANCE_FADES_IN << "u;\n"
        << "const int   CULL_SET_SEEN           = " << static_cast<int>(CullSet::Seen) << ";\n"
        << "const int   CULL_SET_REVEALED       = " << static_cast<int>(CullSet::Revealed) << ";\n"
        << "const int   CULL_SET_SHOWN          = " << static_cast<int>(CullSet::Shown) << ";\n"
        << "const float VIEWMODEL_DEPTH_SHARE   = " << number(GLDepth::VIEWMODEL_SHARE) << ";\n"
        << "const float NEAR_DEPTH              = " << number(GLDepth::EYE_NEAR) << ";\n"
        << "const float FAR_DEPTH               = " << number(GLDepth::EYE_CLEAR) << ";\n"
        << "const vec2  TRANSMITTANCE_LUT_SIZE  = vec2(" << GLAtmosphere::TRANSMITTANCE_WIDTH << ".0, " << GLAtmosphere::TRANSMITTANCE_HEIGHT << ".0);\n"
        << "const vec2  MULTISCATTERING_LUT_SIZE = vec2(" << GLAtmosphere::MULTISCATTERING_SIZE << ".0, " << GLAtmosphere::MULTISCATTERING_SIZE << ".0);\n"
        << "const vec2  SKY_VIEW_LUT_SIZE       = vec2(" << GLAtmosphere::SKY_VIEW_WIDTH << ".0, " << GLAtmosphere::SKY_VIEW_HEIGHT << ".0);\n";

    // The composite pass switches on these; see VKM_RENDER_MODES. The mask has
    // a bit set for each mode the forward pass shades as radiance.
    int mode = 0;
    uint32_t shaded = 0;
#define VKM_RENDER_MODE_GLSL(name, label, glsl, isShaded) \
    if (isShaded) shaded |= 1u << mode;                   \
    out << "const int   MODE_" #glsl " = " << mode++ << ";\n";
    VKM_RENDER_MODES(VKM_RENDER_MODE_GLSL)
#undef VKM_RENDER_MODE_GLSL
    out << "const int   MODE_SHADED_MASK = " << shaded << ";\n";

    // ...and the display transforms it ends on; see VKM_TONEMAPS.
    int tonemap = 0;
#define VKM_TONEMAP_GLSL(name, label, glsl) \
    out << "const int   TONEMAP_" #glsl " = " << tonemap++ << ";\n";
    VKM_TONEMAPS(VKM_TONEMAP_GLSL)
#undef VKM_TONEMAP_GLSL

    // gl_bindings.h's points and units, so a shader names rather than repeats them. Macros, not
    // consts: before GLSL 4.40 a layout qualifier takes only an integer literal.
#define VKM_GL_BINDING_GLSL(group, name, value, glsl, doc) \
    out << "#define " #glsl " " << GLBindings::group::name << "\n";
    VKM_GL_BINDINGS(VKM_GL_BINDING_GLSL)
#undef VKM_GL_BINDING_GLSL

    // The twilight band the sky's night rises through, in the sine of the
    // elevation the shader compares.
    out << "const float SKY_TWILIGHT = "
        << number(std::sin(glm::radians(NightSkySettings::TWILIGHT_DEGREES))) << ";\n";

    // The atmosphere's fixed terms, which no authored scale changes; its scattering depends on
    // the sky's scales and arrives as uniforms (Atmosphere::coefficients).
    {
        namespace A = Atmosphere;
        const glm::vec3 ozone = A::OZONE_ABSORPTION;
        out << "const float ATMOSPHERE_PLANET_RADIUS     = " << number(A::PLANET_RADIUS) << ";\n"
            << "const float ATMOSPHERE_TOP_RADIUS        = " << number(A::TOP_RADIUS) << ";\n"
            << "const float ATMOSPHERE_EYE_ALTITUDE      = " << number(A::EYE_ALTITUDE) << ";\n"
            << "const float ATMOSPHERE_RAYLEIGH_HEIGHT   = " << number(A::RAYLEIGH_SCALE_HEIGHT) << ";\n"
            << "const float ATMOSPHERE_MIE_HEIGHT        = " << number(A::MIE_SCALE_HEIGHT) << ";\n"
            << "const float ATMOSPHERE_OZONE_ALTITUDE    = " << number(A::OZONE_ALTITUDE) << ";\n"
            << "const float ATMOSPHERE_OZONE_HALF_WIDTH  = " << number(A::OZONE_HALF_WIDTH) << ";\n"
            << "const vec3  ATMOSPHERE_OZONE_ABSORPTION  = vec3("
            << number(ozone.r) << ", " << number(ozone.g) << ", " << number(ozone.b) << ");\n"
            << "const float ATMOSPHERE_GROUND_ALBEDO     = " << number(A::GROUND_ALBEDO) << ";\n"
            << "const int   ATMOSPHERE_TRANSMITTANCE_STEPS = " << A::TRANSMITTANCE_STEPS << ";\n"
            << "const float ATMOSPHERE_MIE_G             = " << number(A::MIE_G) << ";\n";
    }

    // The light and material types and particle blends the shaders switch on, from the enums.
    using LightmapKind = GLLightmapBaker::LightKind;
    out << "const int   LIGHT_DIRECTIONAL   = " << static_cast<int>(LightType::Directional)    << ";\n"
        << "const int   LIGHT_POINT         = " << static_cast<int>(LightType::Point)          << ";\n"
        << "const int   LIGHT_SPOT          = " << static_cast<int>(LightType::Spot)           << ";\n"
        << "const int   MAT_OPAQUE          = " << static_cast<int>(MaterialType::Opaque)      << ";\n"
        << "const int   MAT_TRANSPARENT     = " << static_cast<int>(MaterialType::Transparent) << ";\n"
        << "const int   MAT_UNLIT           = " << static_cast<int>(MaterialType::Unlit)       << ";\n"
        << "const int   MAT_ALPHA_MASK      = " << static_cast<int>(MaterialType::AlphaMask)   << ";\n"
        << "const int   MATERIAL_MAX_LAYERS = " << MaterialAsset::MAX_LAYERS                  << ";\n"
        << "const int   LAYER_COPY_FILL     = " << static_cast<int>(GLLayerMaps::Copy::Fill)   << ";\n"
        << "const int   LAYER_COPY_ALBEDO   = " << static_cast<int>(GLLayerMaps::Copy::Albedo) << ";\n"
        << "const int   LAYER_COPY_NORMAL   = " << static_cast<int>(GLLayerMaps::Copy::Normal) << ";\n"
        << "const int   LAYER_COPY_AO_ROUGHNESS_METALLIC = "
        << static_cast<int>(GLLayerMaps::Copy::AoRoughnessMetallic) << ";\n"
        << "const int   LAYER_COPY_ROUGHNESS_METALLIC = "
        << static_cast<int>(GLLayerMaps::Copy::RoughnessMetallic) << ";\n"
        << "const int   PARTICLE_ADDITIVE   = " << static_cast<int>(ParticleBlend::Additive)   << ";\n"
        << "const int   PARTICLE_ALPHA      = " << static_cast<int>(ParticleBlend::Alpha)      << ";\n"
        << "const int   PARTICLE_DISTORTION = " << static_cast<int>(ParticleBlend::Distortion) << ";\n"
        << "const uint  DEBUG_GHOSTED       = " << static_cast<int>(DebugDepth::Ghosted)       << "u;\n"
        << "const uint  DEBUG_HIDDEN        = " << static_cast<int>(DebugDepth::Hidden)        << "u;\n"
        << "const uint  DEBUG_ON_TOP        = " << static_cast<int>(DebugDepth::OnTop)         << "u;\n"
        << "const int   LIGHTMAP_KIND_SUN   = " << static_cast<int>(LightmapKind::Sun)   << ";\n"
        << "const int   LIGHTMAP_KIND_SPOT  = " << static_cast<int>(LightmapKind::Spot)  << ";\n"
        << "const int   LIGHTMAP_KIND_POINT = " << static_cast<int>(LightmapKind::Point) << ";\n"
        << "const int   LIGHTMAP_KIND_SKY   = " << static_cast<int>(LightmapKind::Sky)   << ";\n"
        << "const int   RELIGHT_DIRECTIONAL_HEAD = " << GLIrradianceBaker::RELIGHT_DIRECTIONAL_HEAD << ";\n"
        << "const int   RELIGHT_OUTSIDE_HEAD     = " << GLIrradianceBaker::RELIGHT_OUTSIDE_HEAD << ";\n"
        << "const int   RELIGHT_CELL_HEADS       = " << GLIrradianceBaker::RELIGHT_CELL_HEADS << ";\n";
    static_assert(static_cast<int>(LightType::Count) == 3, "A light type the shaders are not told about");
    static_assert(static_cast<int>(MaterialType::Count) == 4, "A material type the shaders are not told about");
    static_assert(static_cast<int>(ParticleBlend::Count) == 3, "A particle blend the shaders are not told about");
    static_assert(static_cast<int>(DebugDepth::Count) == 3, "A debug depth mode the shaders are not told about");
    static_assert(static_cast<int>(GLLayerMaps::Copy::Count) == 5, "A layer copy the shaders are not told about");
    static_assert(static_cast<int>(LightmapKind::Count) == 4, "A lightmap light kind the shaders are not told about");

    return out.str();
}

} // namespace Vkm::Engine
