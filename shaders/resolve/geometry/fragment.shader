/**
 * Geometry resolve: the multisample depth and G-buffer, one sample of each.
 *
 * Along a silhouette an averaged depth or octahedral normal belongs to neither surface (two
 * encodings blend into a third direction), so both take sample 0: one surface that exists.
 */
layout(binding = POST_SLOT_SCENE_DEPTH)   uniform sampler2DMS u_depth;
layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2DMS u_gbuffer;

layout(location = OUT_GBUFFER) out vec4 GBuffer;

void main() {
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    gl_FragDepth = texelFetch(u_depth, texel, 0).r;
    GBuffer      = texelFetch(u_gbuffer, texel, 0);
}
