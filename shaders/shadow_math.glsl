/*
 * The soft-shadow arithmetic that touches no map: the disk a search or filter takes its taps
 * from, a gather's weights, and the distance a perspective map's depth stands for.
 */

#include "constants.glsl"

// One tap of a Vogel disk. Never turned by per-pixel noise, whose grain only a temporal filter
// hides: a disk dense enough that its 2x2 compares overlap gives a smooth edge unturned.
vec2 vogelTap(int i, int n) {
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * GOLDEN_ANGLE;
    return r * vec2(cos(theta), sin(theta));
}

// The bilinear weights of a gather at @p at in a map @p size texels across, in its order:
// (0,1) (1,1) (1,0) (0,0) of the footprint.
vec4 gatherWeights(vec2 at, vec2 size) {
    vec2 f = fract(at * size - 0.5);
    return vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
}

// The distance along a perspective map's axis its window depth @p depth stores.
float perspectiveAxis(float nearPlane, float farPlane, float depth) {
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}
