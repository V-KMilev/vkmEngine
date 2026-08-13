/*
 * Shared depth reconstruction + exponential slice mapping.
 *
 * All helpers take their projection inputs as parameters so graphics and
 * compute stages run identical code. The slice pair is the single source of
 * the Forward+/froxel depth slicing: the cull compute, the forward cluster
 * lookup, and the fog passes all route through it and must stay bit-identical,
 * or lights pop at slice borders.
 */

// Window-space depth (0..1) -> positive linear view depth, via the perspective
// projection's two coefficients.
float linearizeViewDepth(float depth01, mat4 projection) {
    float ndc = depth01 * 2.0 - 1.0;
    return projection[3][2] / (ndc + projection[2][2]);
}

// Screen UV (0..1) + window depth -> view-space position.
vec3 viewPosFromDepth(vec2 uv, float depth01, mat4 invProjection) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 v    = invProjection * clip;
    return v.xyz / v.w;
}

// Screen UV (0..1) + window depth -> world position.
vec3 worldPosFromDepth(vec2 uv, float depth01, mat4 invViewProj) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 w    = invViewProj * clip;
    return w.xyz / w.w;
}

// Exponential slice coordinate -> positive linear view depth (pass slice + 0.5
// for a slice centre, slice + 1.0 for its far bound).
float sliceToViewDepth(float slice, float zNear, float zFar, float numSlices) {
    return zNear * pow(zFar / zNear, slice / numSlices);
}

// Positive linear view depth -> continuous exponential slice coordinate,
// guarded at the near plane. Callers floor/clamp to index.
float viewDepthToSlice(float viewDepth, float zNear, float zFar, float numSlices) {
    return log(max(viewDepth, zNear) / zNear) / log(zFar / zNear) * numSlices;
}
