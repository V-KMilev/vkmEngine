/*
 * Depth reconstruction and the view's slice mappings, taking projection inputs
 * as parameters so graphics and compute stages run identical code. The
 * exponential pair is the one definition of Forward+/froxel slicing: slicing any
 * other way disagrees with the cluster cull and lights pop at slice borders. The
 * squared pair slices the aerial-perspective volume.
 *
 * The eye's window depth is reversed (GLDepth, convention/gl_depth.h) and clip z / w
 * is the window depth itself; a shadow map's is not. NEAR_DEPTH and FAR_DEPTH are the
 * prelude's GLDepth::EYE_NEAR and GLDepth::EYE_CLEAR: the eye's depth is cleared to
 * the far plane's, and the sky is there.
 */

// Whether a window depth is the far plane's: nothing was drawn there.
bool isFarDepth(float depth01) {
    return depth01 <= FAR_DEPTH;
}

// The nearer and the farther of two window depths.
float nearerDepth(float a, float b) {
    return max(a, b);
}

float fartherDepth(float a, float b) {
    return min(a, b);
}

// Window depth (0..1) -> positive linear view depth through the inverse
// projection, so perspective and orthographic read the same. Only the z and w
// rows matter: view depth does not depend on screen xy.
float linearizeViewDepth(float depth01, mat4 invProjection) {
    float z = invProjection[2][2] * depth01 + invProjection[3][2];
    float w = invProjection[2][3] * depth01 + invProjection[3][3];
    return -z / w;
}

// The sky's linear view depth: past anything a pass measures, so no ray meets
// it and it occludes nothing, yet finite, so an average over it stays a number.
const float SKY_DEPTH = 1.0e6;

// Like linearizeViewDepth, the sky as SKY_DEPTH.
float linearSceneDepth(float depth01, mat4 invProjection) {
    return isFarDepth(depth01) ? SKY_DEPTH : linearizeViewDepth(depth01, invProjection);
}

// Screen UV (0..1) + window depth -> world position.
vec3 worldPosFromDepth(vec2 uv, float depth01, mat4 invViewProj) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth01, 1.0);
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

// Aerial-perspective slice coordinate (slice + 0.5 for a centre) -> positive linear view depth.
// Squared over @p reach, so the near slices are metres deep and the far ones kilometres
// (Hillaire 2020).
float aerialSliceToViewDepth(float slice, float numSlices, float reach) {
    float w = slice / numSlices;
    return reach * w * w;
}

// Positive linear view depth -> continuous aerial-perspective slice coordinate.
float viewDepthToAerialSlice(float viewDepth, float numSlices, float reach) {
    return sqrt(max(viewDepth, 0.0) / reach) * numSlices;
}
