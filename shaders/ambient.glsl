/*
 * The diffuse light the environment sends a point: the baked irradiance volume
 * where it covers the point, else the sky's irradiance, else a flat floor.
 * A shader blending reflection probes into the sky term reads the pieces instead
 * of environmentIrradiance. GLPass::bindAmbient fills the uniforms here.
 */

#include "constants.glsl"
#include "irradiance_volume.glsl"

// Diffuse irradiance, as radiance (E / PI).
layout(binding = IBL_SLOT_IRRADIANCE) uniform samplerCube u_irradiance;
uniform int   u_hasIBL;        // 0 until a sky or an HDR has been baked
uniform float u_iblIntensity;

// What a frame with no baked environment is lit by, as radiance.
const float FLAT_AMBIENT = 0.03;

// Multi-bounce occlusion (Jimenez 2016): a brighter surface bounces more light
// back out of a crease, so it darkens less. Never below single-bounce visibility.
vec3 multiBounceOcclusion(float visibility, vec3 albedo) {
    vec3 a =  2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c =  2.7552 * albedo + 0.6903;
    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

// Diffuse irradiance over PI at @p worldPos around @p n, at the environment's
// intensity.
vec3 environmentIrradiance(vec3 worldPos, vec3 n) {
    vec3 irradiance = u_hasIBL == 1 ? texture(u_irradiance, n).rgb : vec3(FLAT_AMBIENT);
    if (u_hasIrradianceVolume == 1) {
        float w = irradianceVolumeWeight(worldPos);
        if (w > 0.0) {
            vec3 volume = sampleIrradianceVolume(worldPos, n) / PI * u_ivIntensity;
            irradiance = mix(irradiance, volume, w);
        }
    }
    return irradiance * u_iblIntensity;
}
