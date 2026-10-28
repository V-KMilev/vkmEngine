/*
 * Lightmaps (GLLightmaps): where in its page each object's lightmap coordinates land, and the
 * shadows fixed meshes cast in a fixed light's light (Light::movable off), baked there a channel
 * a light. Must match GLLightmaps::ObjectLightmap.
 */

struct ObjectLightmap {
    vec4  scaleOffset;  // xy = scale, zw = offset, from the mesh's lightmap coordinates into the page
    ivec4 page;         // x = the page, -1 for none
};

layout(std430, binding = SSBO_INSTANCE_LIGHTMAP) readonly buffer InstanceLightmaps {
    ObjectLightmap b_lightmaps[];
};

layout(binding = LIGHTMAP_SLOT_MASK) uniform sampler2DArray u_lightmapMask;

uniform int u_hasLightmaps;  // 1 while any object has a page (GLLightmaps::any)

// A baked shadow this near lit counts as lit, so a shadow there now is one the bake never saw:
// something that moves.
const float BAKED_LIT  = 0.99;
// One this near dark is the umbra, whatever else casts there.
const float BAKED_DARK = 0.01;

#include "bspline.glsl"

// The baked shadows at @p uv of @p page, a fixed light a channel: 1 lit, 0 in its shadow. Where a
// pixel spans @p texels of the page, read from the level that size; where it spans less, B-spline
// filtered: a texel is a hand's width, and a sharp edge read bilinear up close shows its steps.
vec4 lightmapShadows(vec2 uv, int page, vec2 texels) {
    const float layer = float(page);
    const float span  = max(texels.x, texels.y);
    if (span >= 1.0) return textureLod(u_lightmapMask, vec3(uv, layer), log2(span));
    vec2 at0, at1, g0, g1;
    bsplineReads(uv, vec2(textureSize(u_lightmapMask, 0).xy), at0, at1, g0, g1);
    return bsplineCombine(
        g0,
        g1,
        textureLod(u_lightmapMask, vec3(at0.x, at0.y, layer), 0.0),
        textureLod(u_lightmapMask, vec3(at1.x, at0.y, layer), 0.0),
        textureLod(u_lightmapMask, vec3(at0.x, at1.y, layer), 0.0),
        textureLod(u_lightmapMask, vec3(at1.x, at1.y, layer), 0.0)
    );
}
