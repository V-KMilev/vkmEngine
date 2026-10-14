/*
 * The G-buffer the depth prepass writes: an octahedral view-space normal, with no normal map. The
 * sky holds the clear, zero, which no visible surface encodes to (it faces away from the eye).
 * Alpha-masked geometry is not in the prepass, so under it a texel holds what is behind.
 */

#include "normal_codec.glsl"

layout(binding = POST_SLOT_SCENE_GBUFFER) uniform sampler2D u_sceneGBuffer;

// Whether a surface wrote @p texel.
bool sceneHasNormal(ivec2 texel) {
    return texelFetch(u_sceneGBuffer, texel, 0).rg != vec2(0.0);
}

// The view-space normal at @p texel.
vec3 sceneNormalAt(ivec2 texel) {
    return octDecode(texelFetch(u_sceneGBuffer, texel, 0).rg);
}

// The view-space normal at @p uv, as the sampler filters it.
vec3 sceneNormalAt(vec2 uv) {
    return octDecode(texture(u_sceneGBuffer, uv).rg);
}
