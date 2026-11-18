/*
 * GTAO visibility in its 8-bit channel. One oblique slice of an open surface
 * integrates to cos(n) + n sin(n), up to half PI; only the average comes to one,
 * so the raw integral is stored scaled and nothing clamps it before the denoise.
 * And the depth band the denoise and the upsample both weigh a neighbor's result by.
 */

#include "../../include/constants.glsl"

const float VISIBILITY_RANGE = 0.5 * PI;  // the largest one slice integrates to

float encodeVisibility(float visibility) { return visibility / VISIBILITY_RANGE; }
float decodeVisibility(float stored)     { return stored * VISIBILITY_RANGE; }

// Distance off a depth, as a fraction of it, where a neighbor's result stops counting.
const float DEPTH_TOLERANCE = 0.02;

// How far off view depth @p z another may lie and still count: past it, its weight is 0.
float depthBand(float z) { return max(z * DEPTH_TOLERANCE, 1e-4); }
