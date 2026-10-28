/**
 * Auto-exposure's histogram (GLExposure): each group counts its pixels of the scene's HDR color
 * into bins of log2 luminance, then adds its counts to the frame's. A pixel every EXPOSURE_STRIDE
 * each way.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;
layout(std430, binding = SSBO_EXPOSURE_HISTOGRAM) buffer Histogram {
    uint bins[EXPOSURE_BINS];
} u_histogram;

#include "../../color.glsl"  // luma
#include "../bins.glsl"      // binOf

shared uint s_bins[EXPOSURE_BINS];

void main() {
    const uint lane  = gl_LocalInvocationIndex;
    const uint lanes = uint(GROUP_IMAGE * GROUP_IMAGE);
    for (uint b = lane; b < uint(EXPOSURE_BINS); b += lanes) s_bins[b] = 0u;
    barrier();

    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy) * EXPOSURE_STRIDE;
    if (all(lessThan(texel, textureSize(u_sceneColor, 0)))) {
        const float lum = luma(texelFetch(u_sceneColor, texel, 0).rgb);
        atomicAdd(s_bins[binOf(lum)], 1u);
    }
    barrier();

    for (uint b = lane; b < uint(EXPOSURE_BINS); b += lanes) {
        if (s_bins[b] != 0u) atomicAdd(u_histogram.bins[b], s_bins[b]);
    }
}
