/*
 * GTAO visibility in its 8-bit channel. One oblique slice of an open surface
 * integrates to cos(n) + n sin(n), up to half PI; only the average comes to one,
 * so the raw integral is stored scaled and nothing clamps it before the denoise.
 */

#include "../constants.glsl"

const float VISIBILITY_RANGE = 0.5 * PI;  // the largest one slice integrates to

float encodeVisibility(float visibility) { return visibility / VISIBILITY_RANGE; }
float decodeVisibility(float stored)     { return stored * VISIBILITY_RANGE; }
