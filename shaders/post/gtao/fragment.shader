/**
 * Ground-truth-style ambient occlusion (horizon hemisphere sampling).
 *
 * Works entirely in view space from the prepass normal/position MRT, so no
 * inverse-projection is needed. Output is a single AO factor in [0,1]
 * (1 = unoccluded) the forward PBR pass multiplies into the ambient term.
 * Interleaved-gradient rotation hides banding without a separate blur pass.
 */
#version 420 core

in vec2 vUV;

out float FragColor;

uniform sampler2D u_normalTex;
uniform sampler2D u_posTex;

uniform float u_radius;     // world-space sample radius
uniform float u_intensity;  // occlusion strength
uniform float u_bias;       // self-occlusion guard (view-space)
uniform float u_power;      // contrast curve
uniform float u_proj11;     // camera projection [1][1] (radius -> screen)

const int NUM_DIR  = 6;
const int NUM_STEP = 6;
const float PI = 3.14159265359;

float interleavedGradient(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

void main() {
    vec3 P = texture(u_posTex, vUV).xyz;
    if (dot(P, P) < 1e-8) {     // cleared (0,0,0) background - nothing to occlude
        FragColor = 1.0;
        return;
    }

    vec3 p = P;
    vec3 n = normalize(texture(u_normalTex, vUV).xyz);

    // World radius -> screen-space UV radius at this depth.
    float uvRadius = u_radius * u_proj11 / (2.0 * max(-p.z, 1e-3));

    float rnd = interleavedGradient(gl_FragCoord.xy);

    float occ = 0.0;
    float total = 0.0;
    for (int d = 0; d < NUM_DIR; ++d) {
        float ang = (float(d) + rnd) * (2.0 * PI / float(NUM_DIR));
        vec2 dir = vec2(cos(ang), sin(ang));

        for (int s = 1; s <= NUM_STEP; ++s) {
            float t = float(s) / float(NUM_STEP);
            vec2 uv = vUV + dir * uvRadius * t;
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;

            vec3 S = texture(u_posTex, uv).xyz;
            total += 1.0;
            if (dot(S, S) < 1e-8) continue;

            vec3 diff = S - p;
            float dist = length(diff);
            if (dist < 1e-4) continue;

            float ndl = max(dot(n, diff / dist) - u_bias, 0.0);
            float range = smoothstep(1.0, 0.0, dist / u_radius);
            occ += ndl * range;
        }
    }

    float ao = 1.0 - u_intensity * (occ / max(total, 1.0));
    FragColor = clamp(pow(max(ao, 0.0), u_power), 0.0, 1.0);
}
