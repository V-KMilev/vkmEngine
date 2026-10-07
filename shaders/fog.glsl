/*
 * What lies between the eye and a point: the sky's air and, nearer, the froxel fog. Each holds the
 * light it scatters toward the eye (rgb) and the transmittance of the light from behind it (a),
 * the fog per froxel of its integrated volume (shaders/fog/integrate), the air per froxel of the
 * aerial-perspective volume (shaders/atmosphere/aerial_perspective).
 *
 * A surface is fogged at its own depth: colour * a + rgb, right for an alpha-blended one too, whose
 * blend scales both terms. An additive one, added to fog already behind it, takes only colour * a.
 * The air lies beneath the fog: (colour * T_air + S_air) * T_fog + S_fog.
 */

#include "depth.glsl"
#include "camera.glsl"

layout(binding = POST_SLOT_FOG_VOLUME)         uniform sampler3D u_fog;
layout(binding = POST_SLOT_AERIAL_PERSPECTIVE) uniform sampler3D u_aerialPerspective;
uniform int   u_hasFog;    // 0 when no volume was integrated this frame, and in an offline render
uniform float u_fogDepth;  // view depth the slices reach (GLFogVolume::depth)
uniform int   u_hasAir;    // 0 when the Atmosphere pass made no volume this frame, and offline
uniform float u_airDepth;  // view depth its slices reach (GLAtmosphere::aerialDepth)

// The froxel fog's scattered light (rgb) and transmittance (a) to the point at screen uv (0..1)
// and positive linear view depth; none when there is no fog.
vec4 localFogAt(vec2 uv, float viewDepth) {
    if (u_hasFog == 0) return vec4(0.0, 0.0, 0.0, 1.0);

    // Slice z holds the fog up to its far bound, so a point s slices deep reads texel s - 1, at
    // coordinate (s - 0.5) / Z. Past the last slice it reads that one: fog to the volume's reach,
    // and nothing beyond.
    float slices = float(textureSize(u_fog, 0).z);
    float slice  = viewDepthToSlice(min(viewDepth, u_fogDepth), u_camera.zNear, u_fogDepth, slices);
    float w      = (slice - 0.5) / slices;
    return texture(u_fog, vec3(uv, w));
}

// The sky's air to the same point: its light (rgb) and mean transmittance (a).
vec4 airAt(vec2 uv, float viewDepth) {
    if (u_hasAir == 0) return vec4(0.0, 0.0, 0.0, 1.0);

    // Each texel holds the air to its slice's centre. Nearer than the first centre the air fades
    // to none at the eye; past the last, the last slice's.
    float slices = float(textureSize(u_aerialPerspective, 0).z);
    float slice  = viewDepthToAerialSlice(viewDepth, slices, u_airDepth);
    vec4  air    = texture(u_aerialPerspective, vec3(uv, max(slice, 0.5) / slices));
    return mix(vec4(0.0, 0.0, 0.0, 1.0), air, clamp(slice * 2.0, 0.0, 1.0));
}

// Everything in front of the point at screen uv and positive linear view depth.
vec4 fogAt(vec2 uv, float viewDepth) {
    vec4 fog = localFogAt(uv, viewDepth);
    vec4 air = airAt(uv, viewDepth);
    return vec4(air.rgb * fog.a + fog.rgb, air.a * fog.a);
}

// Everything in front of the fragment being shaded, at its own depth.
vec4 fragmentFog() {
    return fogAt(
        gl_FragCoord.xy / u_camera.viewport,
        linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection)
    );
}
