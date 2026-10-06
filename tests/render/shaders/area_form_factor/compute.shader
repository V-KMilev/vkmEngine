/**
 * Diffuse form factor of a square emitter facing down onto the point below its
 * centre, per normal; tests/render/render_tests.cpp checks it against the closed form.
 */
layout(local_size_x = 2) in;

#include "../../../../shaders/ltc.glsl"

layout(std430, binding = 0) buffer FormFactors {
    vec4  normal[2];      // in
    float halfSize;       // in: the square's half-extent
    float height;         // in: how far above the point it hangs
    float pad[2];
    float formFactor[2];  // out
} u_case;

void main() {
    const uint i = gl_LocalInvocationID.x;
    const vec3 N = u_case.normal[i].xyz;
    const float a = u_case.halfSize;
    const float h = u_case.height;

    // Counter-clockwise from below, as shaders/forward/pbr/fragment.shader winds a rect.
    const mat3 toLocal = ltcTangentFrame(N);
    const vec3 p0 = normalize(toLocal * vec3(-a, h, -a));
    const vec3 p1 = normalize(toLocal * vec3(-a, h,  a));
    const vec3 p2 = normalize(toLocal * vec3( a, h,  a));
    const vec3 p3 = normalize(toLocal * vec3( a, h, -a));
    const vec3 F = ltcEdgeIntegral(p0, p1) + ltcEdgeIntegral(p1, p2)
        + ltcEdgeIntegral(p2, p3) + ltcEdgeIntegral(p3, p0);
    u_case.formFactor[i] = horizonClippedFormFactor(F);
}
