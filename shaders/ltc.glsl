/*
 * An area light's diffuse form factor by LTC (Heitz 2016), cosine untransformed:
 * the outline in a frame with the normal at +Z, edges summed into the vector
 * form factor, clipped to the horizon. No LTC GGX specular here.
 */

#include "constants.glsl"

// One edge's share of the vector form factor. The constants are Hill's stable
// fit of theta/sin(theta) / 2pi, free of the discontinuity near v1.v2 == -1
// (Heitz 2016 supplement).
vec3 ltcEdgeIntegral(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = (x > 0.0)
        ? v
        : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * thetaSinTheta;
}

// F clipped to the horizon by Hill's same-F sphere: close while the emitter is
// above the horizon, conservative across it, zero once wholly below.
float horizonClippedFormFactor(vec3 F) {
    float len = length(F);
    return max((len * len + F.z) / (len + 1.0), 0.0);
}

// World-to-tangent rows (T1, T2, N). A cosine lobe is symmetric about N, so T1
// comes from N alone; one from the view would vanish looking down the normal.
mat3 ltcTangentFrame(vec3 N) {
    vec3 helper = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T1 = normalize(cross(helper, N));
    vec3 T2 = cross(N, T1);
    return transpose(mat3(T1, T2, N));
}

// A rect or disk light's diffuse form factor at p around N, horizon clipped, and its emitting
// area. centre and the half axes U and V are the light's (U x V out of its emitting face);
// facing is p's side of it, negative behind a two-sided one.
float areaFormFactor(int type, vec3 centre, vec3 U, vec3 V, vec3 p, vec3 N, float facing, out float area) {
    // Seen from the lit side, the corners (bl -> tl -> tr -> br) wind
    // counter-clockwise about N at local +Z, as the edge sum counts positive.
    mat3 toLocal = ltcTangentFrame(N);
    vec3 F = vec3(0.0);
    if (type == LIGHT_RECT) {
        vec3 p0 = normalize(toLocal * ((centre - U - V) - p));
        vec3 p1 = normalize(toLocal * ((centre - U + V) - p));
        vec3 p2 = normalize(toLocal * ((centre + U + V) - p));
        vec3 p3 = normalize(toLocal * ((centre + U - V) - p));
        F = ltcEdgeIntegral(p0, p1) + ltcEdgeIntegral(p1, p2)
            + ltcEdgeIntegral(p2, p3) + ltcEdgeIntegral(p3, p0);
        area = 4.0 * length(U) * length(V);
    } else {
        // Twelve vertices read as circular.
        const int N_DISK = 12;
        vec3 verts[N_DISK];
        for (int dv = 0; dv < N_DISK; ++dv) {
            float t = -float(dv) / float(N_DISK) * 2.0 * PI;  // CW order
            verts[dv] = normalize(toLocal * ((centre + cos(t) * U + sin(t) * V) - p));
        }
        for (int de = 0; de < N_DISK; ++de) {
            F += ltcEdgeIntegral(verts[de], verts[(de + 1) % N_DISK]);
        }
        area = PI * dot(U, U);
    }
    if (facing < 0.0) F = -F;
    return horizonClippedFormFactor(F);
}
