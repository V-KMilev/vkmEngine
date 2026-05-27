/**
 * Weighted-Blended OIT resolve (McGuire-Bavoil 2013).
 *
 * Inputs:
 *   u_oitAccum     RGBA16F = sum of (rgb * a * w, a * w)
 *   u_oitRevealage R8      = product of (1 - a)
 *
 * Output:
 *   color = lerp(accum.rgb / max(accum.a, eps), <dst>, revealage)
 *   alpha = 1 - revealage
 *
 * The host pass binds standard alpha blending (SRC_ALPHA,
 * ONE_MINUS_SRC_ALPHA) so the GL blender combines this fragment's
 * RGB at opacity (1 - revealage) with whatever opaque/sky pixel
 * already lives in the HDR target.
 */
#version 420 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D u_oitAccum;
uniform sampler2D u_oitRevealage;

void main() {
    float revealage = texture(u_oitRevealage, vUV).r;

    // Pixels with no transparent contribution have revealage == 1; skip
    // them entirely so the blend leaves the opaque scene untouched.
    if (revealage >= 1.0 - 1e-5) discard;

    vec4 accum = texture(u_oitAccum, vUV);

    // Guard against the rare divide-by-zero (revealage < 1 but accum.a
    // collapsed to ~0). Treat the contribution as black if the accumulator
    // is degenerate.
    float a = max(accum.a, 1e-4);
    vec3 avgColor = accum.rgb / a;

    FragColor = vec4(avgColor, 1.0 - revealage);
}
