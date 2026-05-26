/**
 * Camera motion blur - reprojection velocity.
 *
 * Per-pixel screen velocity = current UV - reprojected previous UV (world
 * from prepass view-space position via inverse view, then previous
 * view-projection). Averages the resolved HDR along the velocity. Camera-only
 * (no per-object velocity), editor-toggleable, conservative default strength.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_scene;
uniform sampler2D u_viewPos;

uniform mat4  u_invView;
uniform mat4  u_prevViewProj;
uniform float u_strength;
uniform int   u_primed;

const int SAMPLES = 8;

void main() {
    vec3 cur = texture(u_scene, vUV).rgb;

    if (u_primed == 0) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec3 vp = texture(u_viewPos, vUV).xyz;
    if (dot(vp, vp) < 1e-8) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec4 world = u_invView * vec4(vp, 1.0);
    vec4 pc = u_prevViewProj * world;
    if (pc.w <= 0.0) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    vec2 prevUV = (pc.xy / pc.w) * 0.5 + 0.5;

    vec2 velocity = (vUV - prevUV) * u_strength;
    velocity = clamp(velocity, vec2(-0.1), vec2(0.1));
    if (length(velocity) < 1e-4) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec3 sum = vec3(0.0);
    for (int i = 0; i < SAMPLES; ++i) {
        float t = (float(i) / float(SAMPLES - 1)) - 0.5;   // [-0.5, 0.5]
        sum += texture(u_scene, vUV + velocity * t).rgb;
    }

    FragColor = vec4(sum / float(SAMPLES), 1.0);
}
