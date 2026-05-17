/**
 * Auto-exposure adaptation (1x1 R16F, ping-ponged).
 *
 * Reads the geometric-mean scene luminance (top mip of the log-luminance
 * pyramid) and the previous adapted value, then eases toward the target with
 * a frame-rate-independent exponential. The composite turns this into the
 * final exposure (key / adaptedLuminance).
 */
#version 420 core

out vec4 FragColor;

uniform sampler2D u_lumTex;
uniform sampler2D u_prevAdapt;
uniform float u_lumMaxLod;
uniform float u_deltaTime;
uniform float u_speed;

void main() {
    float avgLogLum = textureLod(u_lumTex, vec2(0.5), u_lumMaxLod).r;
    float avgLum = exp2(avgLogLum);

    float prev = texture(u_prevAdapt, vec2(0.5)).r;
    if (prev <= 0.0) prev = avgLum;  // first-frame seed

    float t = 1.0 - exp(-max(u_deltaTime, 0.0) * max(u_speed, 0.001));
    float adapted = prev + (avgLum - prev) * t;

    FragColor = vec4(max(adapted, 1e-4), 0.0, 0.0, 1.0);
}
