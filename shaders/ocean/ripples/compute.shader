/**
 * The ocean's ripple fields, begun (GLOceanFields): a row of each field's grid of wavenumbers,
 * drawn for this moment and transformed along X. ocean/ripples_columns transforms along Z.
 */
layout(local_size_x = GROUP_OCEAN_FFT) in;

#include "../../ocean_ripples.glsl"

layout(binding = 0, rgba32f) uniform writeonly image2DArray u_rows;  // a layer a field

void main() {
    const uint  t     = gl_LocalInvocationID.x;
    const int   row   = int(gl_WorkGroupID.y);
    const int   field = int(gl_WorkGroupID.z);
    const float step  = 2.0 * PI * u_ocean.tiles[field];
    const int   kz    = rippleBin(row);

    // Most rows lie wholly past the field's band, the same for every thread.
    if (float(abs(kz)) * step >= u_ocean.bands[field + 1]) {
        for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
            imageStore(u_rows, ivec3(i, row, field), vec4(0.0));
        }
        return;
    }

    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
        s_line[rippleShuffled(i)] = rippleSpectrum(ivec2(rippleBin(int(i)), kz), field, step);
    }
    rippleTransform(t);
    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
        imageStore(u_rows, ivec3(i, row, field), s_line[i]);
    }
}
