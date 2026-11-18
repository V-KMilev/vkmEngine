/**
 * The ripple fields, begun (GLWaveFields): a row of each field's grid of wavenumbers,
 * drawn for this moment and transformed along X. water/ripples/columns transforms along Z.
 */
layout(local_size_x = GROUP_RIPPLE_FFT) in;

#include "ripples.glsl"

layout(binding = 0, rgba32f) uniform writeonly image2DArray u_rows;  // a layer a field
uniform int u_field;  // the field drawn

void main() {
    const uint  t     = gl_LocalInvocationID.x;
    const int   row   = int(gl_WorkGroupID.y);
    const int   field = u_field;
    const float step  = 2.0 * PI * u_waves.tiles[field];
    const int   kz    = fftBin(row);

    // Most rows lie wholly past the field's band, the same for every thread.
    if (float(abs(kz)) * step >= u_waves.bands[field + 1]) {
        for (uint i = t; i < uint(RIPPLE_FIELD); i += uint(GROUP_RIPPLE_FFT)) {
            imageStore(u_rows, ivec3(i, row, field), vec4(0.0));
        }
        return;
    }

    for (uint i = t; i < uint(RIPPLE_FIELD); i += uint(GROUP_RIPPLE_FFT)) {
        s_fft[fftShuffled(i)] = rippleSpectrum(ivec2(fftBin(int(i)), kz), field, step);
    }
    fftTransform(t);
    for (uint i = t; i < uint(RIPPLE_FIELD); i += uint(GROUP_RIPPLE_FFT)) {
        imageStore(u_rows, ivec3(i, row, field), s_fft[i]);
    }
}
