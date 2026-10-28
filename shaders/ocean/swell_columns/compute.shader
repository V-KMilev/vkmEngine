/**
 * The ocean's swell, finished (GLOceanFields): a column of ocean/swell's rows transformed along Z
 * into the water's movement over one repeat; and each texel's foam, the most its water was squeezed
 * lately less what that has shed since, kept a frame at a time (ocean.glsl, oceanLingering).
 */
layout(local_size_x = GROUP_SWELL_FFT) in;

#include "../../ocean_swell.glsl"

uniform float u_seconds;  // of the sea's clock since the swell was last drawn

void main() {
    const uint t       = gl_LocalInvocationID.x;
    const int  column  = int(gl_WorkGroupID.x);
    const int  cascade = int(gl_WorkGroupID.z);
    const int  base    = cascade * SWELL_FIELD * SWELL_FIELD;

    for (uint i = t; i < uint(SWELL_FIELD); i += uint(GROUP_SWELL_FFT)) {
        const SwellTexel row = b_swell[base + int(i) * SWELL_FIELD + column];
        s_fft[fftShuffled(i)]               = row.moved;
        s_fft[SWELL_FIELD + fftShuffled(i)] = row.slopes;
    }
    fftTransform(t);
    for (uint i = t; i < uint(SWELL_FIELD); i += uint(GROUP_SWELL_FFT)) {
        // Each pair transformed to one field plus i the other: x the first, y the second.
        const vec4 m  = s_fft[i];
        const vec4 s  = s_fft[SWELL_FIELD + i];
        const int  at = base + int(i) * SWELL_FIELD + column;
        b_swell[at] = SwellTexel(vec4(m.y, m.x, m.z, m.w), s);

        // The squeeze, to first order the carry's spread along X and Z, in squeezes it rarely passes;
        // water drawn apart foams none, and takes none from the other cascades' foam.
        const float pressed = u_ocean.squeeze.x > 0.0 ? -(s.z + s.w) / u_ocean.squeeze.x : 0.0;
        b_swellFoam[at] = max(max(b_swellFoam[at] - FOAM_FADE * u_seconds, pressed), 0.0);
    }
}
