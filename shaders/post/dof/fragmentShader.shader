/**
 * Depth of field - depth-aware circle-of-confusion gather.
 *
 * CoC is a smooth band around the focus distance (view-space depth from the
 * prepass). A 16-tap Poisson disc gathers the resolved HDR, but every tap is
 * weighted by ITS OWN CoC, so a sharp in-focus surface never bleeds into a
 * defocused region (no halo around the subject) and the focused silhouette
 * stays clean. Background (no geometry) is treated as fully defocused, so a
 * blurred subject does not get a razor edge against it. Single gather pass
 * (no separable near/far bokeh) - editor-toggleable.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_scene;
uniform sampler2D u_viewPos;

uniform float u_focusDistance;  // view-space metres kept sharp
uniform float u_focusRange;     // distance over which it ramps to full blur
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

// 0 at the focus distance, smoothly -> 1 by focusRange. Missing geometry
// (background) reads as fully defocused so a blurred subject has no hard
// edge against the sky.
float cocAt(vec2 uv) {
    vec3 vp = texture(u_viewPos, uv).xyz;
    if (dot(vp, vp) < 1e-8) return 1.0;
    float dist = abs(-vp.z - u_focusDistance);
    return smoothstep(0.0, max(u_focusRange, 1e-3), dist);
}

void main() {
    vec3  centerCol = texture(u_scene, vUV).rgb;
    float centerCoC = cocAt(vUV);

    if (centerCoC < 0.02) {              // in focus - keep it crisp
        FragColor = vec4(centerCol, 1.0);
        return;
    }

    float radius = centerCoC * u_maxBlur;

    vec3  acc  = centerCol;
    float wsum = 1.0;
    for (int i = 0; i < 16; ++i) {
        vec2  suv  = vUV + POISSON[i] * radius;
        float scoc = cocAt(suv);
        // Only samples at least as defocused as this pixel contribute: a
        // sharp in-focus surface cannot smear outward, killing the halo and
        // keeping the subject's silhouette clean.
        float w = smoothstep(0.0, 1.0, scoc / max(centerCoC, 1e-3));
        acc  += texture(u_scene, suv).rgb * w;
        wsum += w;
    }

    vec3 blurred = acc / wsum;
    FragColor = vec4(mix(centerCol, blurred, centerCoC), 1.0);
}
