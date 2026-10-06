// Depth-only shadow pass into the light's clip space; the caster's model matrix comes by instance index.
// SKINNED (shadow/depth_skinned) poses it first, by the camera path's expression.
layout(location = ATTR_POSITION) in vec3 aPos;
#include "../../instancing.glsl"
#ifdef SKINNED
#include "../../skinning.glsl"
#include "../../skinning_instanced.glsl"
#endif

uniform mat4 u_lightVP;

void main() {
#ifdef SKINNED
    gl_Position = u_lightVP * skinnedWorldPosition(instanceModel(), instanceSkinBase());
#else
    gl_Position = u_lightVP * instanceModel() * vec4(aPos, 1.0);
#endif
}
