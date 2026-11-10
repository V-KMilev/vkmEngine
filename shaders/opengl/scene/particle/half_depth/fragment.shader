/**
 * The scene's depth at half resolution, for the particles drawn there (GLParticlePass): of each
 * 2x2 block the nearest, so a particle behind a silhouette is hidden over the whole block and the
 * composite fills the far side's pixels from the texel beside it. The color starts empty.
 */

#include "../../../include/depth.glsl"

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // the full-resolution copy

layout(location = OUT_COLOR) out vec4 FragColor;

void main() {
    const ivec2 last  = textureSize(u_sceneDepth, 0) - 1;
    const ivec2 first = ivec2(gl_FragCoord.xy) * 2;
    float nearest = texelFetch(u_sceneDepth, first, 0).r;
    nearest = nearerDepth(nearest, texelFetch(u_sceneDepth, min(first + ivec2(1, 0), last), 0).r);
    nearest = nearerDepth(nearest, texelFetch(u_sceneDepth, min(first + ivec2(0, 1), last), 0).r);
    nearest = nearerDepth(nearest, texelFetch(u_sceneDepth, min(first + ivec2(1, 1), last), 0).r);
    gl_FragDepth = nearest;
    FragColor    = vec4(0.0);
}
