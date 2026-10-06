/*
 * An area light's diffuse form factor by LTC (Heitz 2016), cosine untransformed:
 * the outline in a frame with the normal at +Z, edges summed into the vector
 * form factor, clipped to the horizon. No LTC GGX specular here.
 */

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
