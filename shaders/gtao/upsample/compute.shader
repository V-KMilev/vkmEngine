/**
 * GTAO upsample: the denoised half-size result, each pixel of it standing for the scene texel at
 * the corner of its 2x2, widened to the scene's size. A pixel blends the four results round it
 * bilinearly, each weighed by how near the depth it stands for lies to the pixel's own, so
 * occlusion never crosses a silhouette; where none lies near, it takes the nearest in depth.
 *
 * Every pixel's alpha is written 1, unoccluded: GLScreenShadowPass darkens it where it shades.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

// r = shaped AO, gb = oct bent normal, at half size.
layout(binding = POST_SLOT_AO)       uniform sampler2D u_half;
// Linear view depth; level 0 is the scene's size.
layout(binding = POST_SLOT_AO_DEPTH) uniform sampler2D u_depthMips;
layout(binding = 0, rgba8) uniform writeonly image2D u_dst;

#include "../../normal_codec.glsl"

// Distance off the pixel's depth, as a fraction of it, where a result stops counting.
const float TOLERANCE = 0.02;

void main() {
    const ivec2 size  = imageSize(u_dst);
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, size))) return;

    const float z    = texelFetch(u_depthMips, texel, 0).r;
    const float band = max(z * TOLERANCE, 1e-4);
    const ivec2 last = textureSize(u_half, 0) - 1;
    // The result at or before the pixel each way, and how far past it the pixel lies: 0 on the
    // texel it stands for, a half between two.
    const ivec2 corner = texel / 2;
    const vec2  past   = vec2(texel - corner * 2) * 0.5;

    float ao      = 0.0;
    vec3  bent    = vec3(0.0);
    float total   = 0.0;
    float closest = 1e30;
    vec4  nearest = vec4(1.0);
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const ivec2 at     = min(corner + ivec2(i, j), last);
            const vec4  result = texelFetch(u_half, at, 0);
            const float off    = abs(texelFetch(u_depthMips, min(at * 2, size - 1), 0).r - z);
            if (off < closest) {
                closest = off;
                nearest = result;
            }
            const float across = (i == 0 ? 1.0 - past.x : past.x) * (j == 0 ? 1.0 - past.y : past.y);
            const float w      = across * clamp(1.0 - off / band, 0.0, 1.0);
            if (w <= 0.0) continue;
            // Blended decoded: a blend of two octahedral encodings is not the encoding of theirs.
            ao    += result.r * w;
            bent  += octDecode(result.gb) * w;
            total += w;
        }
    }

    if (total <= 0.0 || dot(bent, bent) <= 1e-8) {
        imageStore(u_dst, texel, vec4(nearest.rgb, 1.0));
        return;
    }
    imageStore(u_dst, texel, vec4(ao / total, octEncode(normalize(bent)), 1.0));
}
