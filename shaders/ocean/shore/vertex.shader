// The scene seen from above the ocean, for its shore (GLOceanFields::captureShore): world points.
// SKINNED (ocean/shore_skinned) poses each first, by the camera path's expression.
layout(location = ATTR_POSITION) in vec3 aPos;
#include "../../instancing.glsl"
#ifdef SKINNED
#include "../../skinning.glsl"
#include "../../skinning_instanced.glsl"
#endif

uniform mat4 u_shoreVP;  // looking straight down over the shore's square

out vec3 vWorld;

void main() {
#ifdef SKINNED
    const vec4 world = skinnedWorldPosition(instanceModel(), instanceSkinBase());
#else
    const vec4 world = instanceModel() * vec4(aPos, 1.0);
#endif
    vWorld      = world.xyz;
    gl_Position = u_shoreVP * world;
}
