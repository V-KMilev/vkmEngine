/**
 * Camera motion blur (depth reprojection).
 *
 * Reconstructs each pixel's world position from depth, projects it through the
 * previous frame's view-projection, and takes the screen-space delta as a
 * velocity. The HDR colour is averaged along that velocity. Camera-only: there
 * is no per-object velocity, so geometry moving under a still camera does not
 * streak. The velocity is clamped so a fast turn or a scene cut cannot smear the
 * whole screen.
 */
#version 430 core

in vec2 vUV;

out vec4 FragColor;

layout(binding = 18) uniform sampler2D u_color;  // live HDR scene
layout(binding = 19) uniform sampler2D u_depth;  // scene depth

uniform mat4  u_invViewProj;   // current frame: NDC -> world
uniform mat4  u_prevViewProj;  // previous frame: world -> clip
uniform vec2  u_screenSize;
uniform float u_intensity;     // velocity scale
uniform float u_maxVelocity;   // clamp on the smear length (UV)
uniform int   u_samples;       // taps along the velocity

void main() {
    float depth = texture(u_depth, vUV).r;
    vec3  color = texture(u_color, vUV).rgb;

    // Reconstruct world position, reproject through last frame's camera.
    vec4 ndc      = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH   = u_invViewProj * ndc;
    vec3 world    = worldH.xyz / worldH.w;
    vec4 prevClip = u_prevViewProj * vec4(world, 1.0);
    vec2 prevUV   = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    vec2  velocity = (vUV - prevUV) * u_intensity;
    float len      = length(velocity);
    if (len > u_maxVelocity) velocity *= u_maxVelocity / len;

    // Negligible motion (or a still camera): pass through unblurred.
    if (length(velocity) < 1e-4) { FragColor = vec4(color, 1.0); return; }

    vec3  sum   = color;
    float count = 1.0;
    for (int i = 1; i < u_samples; ++i) {
        float t  = float(i) / float(u_samples - 1) - 0.5;  // centre on this pixel: [-0.5, 0.5]
        vec2  uv = vUV + velocity * t;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        sum   += texture(u_color, uv).rgb;
        count += 1.0;
    }

    FragColor = vec4(sum / count, 1.0);
}
