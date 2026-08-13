/**
 * Screen-space contact shadows for the sun.
 *
 * From each surface, march a short ray toward the sun in view space, projecting
 * each step to screen and comparing against the depth buffer. If geometry within
 * a thickness window occludes the ray, the sun is shadowed. Catches small-scale
 * contact occlusion the cascades are too coarse for. Writes visibility (1 lit ..
 * 0 shadowed); a separate pass, so the depth buffer is read, never read-while-
 * written like a forward-fold would be.
 */

#include "../_common/depth.glsl"

in vec2 vUV;
out vec4 FragColor;

layout(binding = 19) uniform sampler2D u_sceneDepth;  // resolved scene depth

uniform mat4  u_projection;
uniform mat4  u_invProjection;
uniform vec3  u_sunDirView;  // direction TO the sun, view space (normalized)
uniform float u_length;      // ray length (view units)
uniform float u_thickness;   // max occluder thickness

const int STEPS = 12;

void main() {
    float d = texture(u_sceneDepth, vUV).r;
    if (d >= 1.0) { FragColor = vec4(1.0); return; }  // background: fully lit

    vec3  P    = viewPosFromDepth(vUV, d, u_invProjection);
    vec3  step = u_sunDirView * (u_length / float(STEPS));
    vec3  ray  = P + step * 0.5;  // step off the surface to avoid self-occlusion

    float occ = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        ray += step;

        vec4 clip = u_projection * vec4(ray, 1.0);
        vec2 uv   = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;

        vec3  sampleP = viewPosFromDepth(uv, texture(u_sceneDepth, uv).r, u_invProjection);
        float diff    = sampleP.z - ray.z;  // view Z is negative; >0 => surface in front of the ray
        if (diff > 0.015 && diff < u_thickness) { occ = 1.0; break; }
    }

    FragColor = vec4(1.0 - occ);
}
