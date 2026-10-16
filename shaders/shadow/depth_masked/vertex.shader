// A caster cut by its alpha: depth's projection, plus the UV the fragment stage cuts the shadow by.
// SKINNED (shadow/depth_masked_skinned) poses it first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_UV) in vec2 aUV;
#include "../../instancing.glsl"
#include "../../material.glsl"

uniform mat4 u_lightVP;

out vec2 vUV;
flat out uint vCaster;  // the object drawn, so each transparent caster draws its own dither

void main() {
    vUV     = materialUV(aUV);
    vCaster = aInstance;
    gl_Position = u_lightVP * instanceWorldPosition();
}
