/**
 * The ocean's ripple fields, finished (GLOceanFields): a column of ocean/ripples' rows transformed
 * along Z into the surface over one repeat. A texel holds the slopes in x and z, the squeeze, and
 * the slope squared, which mip-maps to the mean square a pixel's footprint spans (ocean_water.glsl,
 * oceanShadeAt).
 */
layout(local_size_x = GROUP_OCEAN_FFT) in;

#include "../../ocean_ripples.glsl"

layout(binding = 0, rgba32f) uniform readonly image2DArray u_rows;
layout(binding = 1, rgba16f) uniform writeonly image2DArray u_fields;  // a layer a field

void main() {
    const uint t      = gl_LocalInvocationID.x;
    const int  column = int(gl_WorkGroupID.x);
    const int  field  = int(gl_WorkGroupID.z);

    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
        s_fft[fftShuffled(i)] = imageLoad(u_rows, ivec3(column, i, field));
    }
    fftTransform(t);
    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
        const vec4 water = s_fft[i];
        imageStore(u_fields, ivec3(column, i, field), vec4(water.xyz, dot(water.xy, water.xy)));
    }
}
