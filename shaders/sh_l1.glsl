/*
 * Real SH-L1 basis + cosine-convolution constants.
 *
 * These lock the projection <-> evaluation contract of the baked irradiance
 * volume: irradiance/project integrates radiance against Y0/Y1, and
 * sampleIrradianceVolume evaluates E = A0*Y0*sh0 + A1*Y1*(n.y*sh1 + n.z*sh2 +
 * n.x*sh3). The two sides must never drift apart, so both take them from here.
 */

#include "constants.glsl"

// SH basis constants (real, normalised).
const float SH_Y0 = 0.282095;  // Y(0, 0)
const float SH_Y1 = 0.488603;  // Y(1, -1) / Y(1, 0) / Y(1, 1) scale

// Lambertian cosine-lobe convolution per band.
const float SH_A0 = PI;
const float SH_A1 = 2.0 * PI / 3.0;
// Band 2's, and its zonal basis scale: Y(2, 0) = SH_Y20 * (3 z^2 - 1). Stored by no bake; the
// lookup predicts the zonal term from L1 (ZH3).
const float SH_A2  = PI / 4.0;
const float SH_Y20 = 0.315392;
