/*
 * The froxel fog between the eye and a point. Per froxel, the integrated volume
 * (shaders/fog/integrate) holds the light scattered toward the eye up to its far bound (rgb) and
 * the transmittance of light from behind it (a).
 *
 * A surface is fogged at its own depth: colour * a + rgb, right for an alpha-blended one too, whose
 * blend scales both terms. An additive one, added to fog already behind it, takes only colour * a.
 */

#include "depth.glsl"
#include "camera.glsl"

layout(binding = POST_SLOT_FOG_VOLUME) uniform sampler3D u_fog;
uniform int   u_hasFog;    // 0 when no volume was integrated this frame, and in an offline render
uniform float u_fogDepth;  // view depth the slices reach (GLFogVolume::depth)

// Scattered light (rgb) and transmittance (a) to the point at screen uv (0..1) and positive linear
// view depth; clear air when there is no fog.
vec4 fogAt(vec2 uv, float viewDepth) {
    if (u_hasFog == 0) return vec4(0.0, 0.0, 0.0, 1.0);

    // Slice z holds the fog up to its far bound, so a point s slices deep reads texel s - 1, at
    // coordinate (s - 0.5) / Z. Past the last slice it reads that one: fog to the volume's reach,
    // and nothing beyond.
    float slices = float(textureSize(u_fog, 0).z);
    float slice  = viewDepthToSlice(min(viewDepth, u_fogDepth), u_camera.zNear, u_fogDepth, slices);
    float w      = (slice - 0.5) / slices;
    return texture(u_fog, vec3(uv, w));
}

// The fog in front of the fragment being shaded, at its own depth.
vec4 fragmentFog() {
    return fogAt(
        gl_FragCoord.xy / u_camera.viewport,
        linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection)
    );
}
