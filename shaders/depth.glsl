/*
 * Depth reconstruction and exponential slice mapping, taking projection inputs
 * as parameters so graphics and compute stages run identical code. The slice
 * pair is the one definition of Forward+/froxel slicing: slicing any other way
 * disagrees with the cluster cull and lights pop at slice borders.
 */

// Window depth (0..1) -> positive linear view depth through the inverse
// projection, so perspective and orthographic read the same. Only the z and w
// rows matter: view depth does not depend on screen xy.
float linearizeViewDepth(float depth01, mat4 invProjection) {
    float ndc = depth01 * 2.0 - 1.0;
    float z   = invProjection[2][2] * ndc + invProjection[3][2];
    float w   = invProjection[2][3] * ndc + invProjection[3][3];
    return -z / w;
}

// The sky's linear view depth: past anything a pass measures, so no ray meets
// it and it occludes nothing, yet finite, so an average over it stays a number.
const float SKY_DEPTH = 1.0e6;

// Like linearizeViewDepth, the sky as SKY_DEPTH.
float linearSceneDepth(float depth01, mat4 invProjection) {
    return depth01 >= 1.0 ? SKY_DEPTH : linearizeViewDepth(depth01, invProjection);
}

// Screen UV (0..1) + window depth -> world position.
vec3 worldPosFromDepth(vec2 uv, float depth01, mat4 invViewProj) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 w    = invViewProj * clip;
    return w.xyz / w.w;
}

// Exponential slice coordinate -> positive linear view depth (slice + 0.5 for a
// centre, slice + 1.0 for the far bound).
float sliceToViewDepth(float slice, float zNear, float zFar, float numSlices) {
    return zNear * pow(zFar / zNear, slice / numSlices);
}

// Positive linear view depth -> continuous slice coordinate, guarded at the
// near plane. Callers floor/clamp to index.
float viewDepthToSlice(float viewDepth, float zNear, float zFar, float numSlices) {
    return log(max(viewDepth, zNear) / zNear) / log(zFar / zNear) * numSlices;
}
