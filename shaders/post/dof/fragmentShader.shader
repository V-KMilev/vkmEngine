/**
 * Depth of field - circle-of-confusion disc blur.
 *
 * CoC from the prepass view-space depth vs a focus distance/range; a 16-tap
 * Poisson disc gathers the resolved HDR scaled by CoC, then lerps sharp to
 * blurred. Background (no geometry) stays sharp. Simple gather (no separable
 * near/far bokeh) - tuned conservative, editor-toggleable.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_scene;
uniform sampler2D u_viewPos;

uniform float u_focusDistance;  // view-space metres in focus
uniform float u_focusRange;     // distance over which it fully defocuses
uniform float u_maxBlur;        // max gather radius in UV

const vec2 POISSON[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

void main() {
    vec3 cur = texture(u_scene, vUV).rgb;

    vec3 vp = texture(u_viewPos, vUV).xyz;
    if (dot(vp, vp) < 1e-8) {            // background stays sharp
        FragColor = vec4(cur, 1.0);
        return;
    }

    float depth = -vp.z;                 // positive view distance
    float coc = clamp(abs(depth - u_focusDistance) / max(u_focusRange, 1e-3), 0.0, 1.0);
    if (coc < 0.01) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    float radius = coc * u_maxBlur;
    vec3 sum = cur;
    for (int i = 0; i < 16; ++i) {
        sum += texture(u_scene, vUV + POISSON[i] * radius).rgb;
    }
    vec3 blurred = sum / 17.0;

    FragColor = vec4(mix(cur, blurred, coc), 1.0);
}
