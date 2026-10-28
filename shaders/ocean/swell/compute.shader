/**
 * The ocean's swell, begun (GLOceanFields): a row of each cascade's grid, its waves rolled to the
 * moment and transformed along X - the carry and its changes in one line, the height's slopes and
 * the carry's in the other. ocean/swell_columns transforms along Z.
 */
layout(local_size_x = GROUP_SWELL_FFT) in;

#include "../../ocean_swell.glsl"

uniform float u_choppiness;  // OceanSwell::choppiness: how far a crest draws the water toward it

void main() {
    const uint  t       = gl_LocalInvocationID.x;
    const int   row     = int(gl_WorkGroupID.y);
    const int   cascade = int(gl_WorkGroupID.z);
    const int   base    = cascade * SWELL_FIELD * SWELL_FIELD;
    const float step    = 2.0 * PI / u_ocean.swell[cascade];
    const float chop    = u_choppiness;

    for (uint i = t; i < uint(SWELL_FIELD); i += uint(GROUP_SWELL_FFT)) {
        const int   col        = int(i);
        const vec4  ahead      = b_spectrum[base + row * SWELL_FIELD + col];
        const vec4  behind     = b_spectrum[base + swellMirror(row) * SWELL_FIELD + swellMirror(col)];
        const vec2  k          = step * vec2(swellBin(col), swellBin(row));
        const float wavenumber = sqrt(dot(k, k));
        vec4 m = vec4(0.0);
        vec4 s = vec4(0.0);
        if (wavenumber > 0.0 && (ahead.z > 0.0 || behind.z > 0.0)) {
            const float turns = max(ahead.z, behind.z);
            const float angle = 2.0 * PI * fract(turns * u_ocean.swell.w);
            const vec2  roll  = vec2(cos(angle), sin(angle));
            const vec2  h     = complexTimes(ahead.xy, vec2(roll.x, -roll.y))
                + complexTimes(vec2(behind.x, -behind.y), roll);
            // The carry is i chop k/|k| h - toward the crest, so crests sharpen and troughs round
            // (Tessendorf's D, its lambda negative) - the height's slope i k h, the carry's change
            // along X and Z -chop k k/|k| h. The slopes packed are i k_x h + i (i k_z h):
            // i k_x h - k_z h.
            const vec2 across = swellTurned(h) / wavenumber * chop;
            const vec2 alongZ = h * (-chop * k.x * k.y / wavenumber);
            m = vec4(swellPacked(h, across * k.x), swellPacked(across * k.y, alongZ));
            s = vec4(
                swellTurned(h) * k.x - h * k.y,
                swellPacked(h * (-chop * k.x * k.x / wavenumber), h * (-chop * k.y * k.y / wavenumber))
            );
        }
        s_fft[fftShuffled(i)]               = m;
        s_fft[SWELL_FIELD + fftShuffled(i)] = s;
    }
    fftTransform(t);
    for (uint i = t; i < uint(SWELL_FIELD); i += uint(GROUP_SWELL_FFT)) {
        b_swell[base + row * SWELL_FIELD + int(i)] = SwellTexel(s_fft[i], s_fft[SWELL_FIELD + i]);
    }
}
