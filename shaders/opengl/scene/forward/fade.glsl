/*
 * The samples of its pixel an instance covers as the eye's draw fades it (instancing.glsl's
 * instanceFade). A fade gives up a share of each pixel's samples, and the level fading in across a
 * hand-over takes exactly those, so together they cover each sample once. Each sample's threshold
 * is the pixel's dither a step apart from the next one's: under MSAA a share thins the samples,
 * which the resolve averages into a cross-fade; at one sample a share thins the pixels. The prepass
 * and the forward pass cover by it alike (fadeCover), so depth and color agree.
 */
#include "../../include/noise.glsl"

// The samples the instance faded by @p fade covers, as gl_SampleMask[0] takes them; all for zero.
int fadeMask(uint fade) {
    if (fade == 0u) return -1;
    const bool  fadesIn = (fade & INSTANCE_FADES_IN) != 0u;
    const uint  step    = (fade >> INSTANCE_OBJECT_BITS) & (INSTANCE_FADE_STEPS - 1u);
    const float given   = float(step) / float(INSTANCE_FADE_STEPS) * float(gl_NumSamples);
    const float dither  = effectNoise(gl_FragCoord.xy, NOISE_FADE);
    int mask = 0;
    for (int s = 0; s < gl_NumSamples; ++s) {
        if ((float(s) + dither < given) == fadesIn) mask |= 1 << s;
    }
    return mask;
}

// Cover only the samples fadeMask gives the instance faded by @p fade: a fragment with none is
// discarded, as a single-sample target ignores gl_SampleMask.
void fadeCover(uint fade) {
    const int mask = fadeMask(fade);
    if (mask == 0) discard;
    gl_SampleMask[0] = mask;
}
