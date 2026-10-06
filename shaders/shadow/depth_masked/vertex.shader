// Alpha-masked caster: depth's projection, plus the UV the fragment stage cuts the shadow by.
// SKINNED (shadow/depth_masked_skinned) poses it first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_UV) in vec2 aUV;
#include "../../instancing.glsl"
#ifdef SKINNED
#include "../../skinning.glsl"
#include "../../skinning_instanced.glsl"
#endif

uniform mat4 u_lightVP;

out vec2 vUV;

void main() {
    vUV = aUV;
#ifdef SKINNED
    gl_Position = u_lightVP * skinnedWorldPosition(instanceModel(), instanceSkinBase());
#else
    gl_Position = u_lightVP * instanceModel() * vec4(aPos, 1.0);
#endif
}
