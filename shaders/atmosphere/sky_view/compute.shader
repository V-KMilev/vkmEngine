/**
 * The sky-view table (Hillaire 2020, section 5.3): the sky's radiance from the eye for every
 * view, drawn each frame for the skybox, in skyViewUnit's latitude/longitude parameterisation.
 *
 * Built in a frame whose sun lies in the XY plane: the sky is symmetric about the sun's vertical
 * plane, so the table's azimuth is measured from the sun's and covers half the circle.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = BAKE_SLOT_TRANSMITTANCE)   uniform sampler2D u_transmittance;
layout(binding = BAKE_SLOT_MULTISCATTERING) uniform sampler2D u_multiScattering;
layout(binding = 0, rgba16f) uniform writeonly image2D u_skyView;

uniform float u_sunZenithCos;    // the sun's elevation, as the cosine of its zenith angle
uniform vec3  u_sunIlluminance;  // above the air: the scene's sunlight before the air dims it

#include "../../atmosphere.glsl"

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, imageSize(u_skyView)))) return;

    vec2  params     = skyViewParams(lutUnit((vec2(texel) + 0.5) / SKY_VIEW_LUT_SIZE, SKY_VIEW_LUT_SIZE));
    float zenithSin  = sqrt(max(1.0 - params.x * params.x, 0.0));
    float azimuthSin = sqrt(max(1.0 - params.y * params.y, 0.0));
    vec3  dir        = vec3(zenithSin * params.y, params.x, zenithSin * azimuthSin);
    vec3  sunDir     = vec3(sqrt(max(1.0 - u_sunZenithCos * u_sunZenithCos, 0.0)), u_sunZenithCos, 0.0);

    vec3 radiance = u_sunIlluminance * skyRadiance(u_transmittance, u_multiScattering, dir, sunDir);
    imageStore(u_skyView, texel, vec4(radiance, 1.0));
}
