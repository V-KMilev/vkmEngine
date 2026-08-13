/**
 * Froxel fog - apply.
 *
 * Blends the integrated fog volume onto the resolved scene: sample the volume at
 * the fragment's froxel (screen UV + exponential depth slice) and composite
 * scene*transmittance + accumulated in-scattering. Sky (depth at the far plane)
 * samples the last slice, so the horizon fades into the fog too.
 */

#include "../../_common/depth.glsl"

in vec2 vUV;
out vec4 FragColor;

layout(binding = 18) uniform sampler2D u_sceneColor;   // resolved HDR scene
layout(binding = 19) uniform sampler2D u_sceneDepth;   // resolved scene depth
layout(binding = 24) uniform sampler3D u_fog;     // integrated fog volume

uniform mat4  u_projection;  // for linear view depth
uniform float u_zNear;
uniform float u_zFar;

void main() {
    vec3 scene = texture(u_sceneColor, vUV).rgb;

    float viewDepth = linearizeViewDepth(texture(u_sceneDepth, vUV).r, u_projection);

    // Normalized exponential froxel W coordinate (the shared slice mapping,
    // numSlices = 1), far-clamped to stay inside the volume.
    float w = viewDepthToSlice(min(viewDepth, u_zFar), u_zNear, u_zFar, 1.0);

    vec4 fog = texture(u_fog, vec3(vUV, w));  // rgb = accumulated scattering, a = transmittance
    FragColor = vec4(scene * fog.a + fog.rgb, 1.0);
}
