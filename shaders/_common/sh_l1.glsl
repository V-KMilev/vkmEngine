/*
 * Real SH-L1 basis + cosine-convolution constants. Requires constants.glsl.
 *
 * These lock the projection <-> evaluation contract of the baked irradiance
 * volume: irradiance/project integrates radiance against Y0/Y1, the forward
 * pass evaluates E = A0*Y0*sh0 + A1*Y1*(n.y*sh1 + n.z*sh2 + n.x*sh3). The two
 * sides must never drift apart, so both include this file.
 */

// SH basis constants (real, normalised).
const float SH_Y0 = 0.282095;  // Y(0, 0)
const float SH_Y1 = 0.488603;  // Y(1, -1) / Y(1, 0) / Y(1, 1) scale

// Lambertian cosine-lobe convolution per band.
const float SH_A0 = PI;
const float SH_A1 = 2.0 * PI / 3.0;
