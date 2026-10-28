// A receiver drawn into its lightmap page (GLLightmapBaker): placed by its lightmap coordinates
// into its square (GLLightmaps::bindBakingObjects), carrying where it lies and the way it faces.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_LIGHTMAP_UV) in vec2 aLightmapUV;
#include "../../instancing.glsl"
#include "../../lightmap.glsl"

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    const ObjectLightmap lightmap = b_lightmaps[aInstance];
    const vec2 at = aLightmapUV * lightmap.scaleOffset.xy + lightmap.scaleOffset.zw;
    vWorldPos   = instanceWorldPosition().xyz;
    vNormal     = normalMatrix(instanceModel()) * aNormal;
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
