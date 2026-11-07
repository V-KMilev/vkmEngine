// A caster cut by its alpha: depth's projection, plus the UV the fragment stage cuts the shadow by.
// SKINNED (the skinned masked depth) poses it first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_UV) in vec2 aUV;
#include "../../../include/instancing.glsl"
#include "../../../include/material.glsl"

uniform mat4 u_lightVP;

out vec2 vUV;
flat out uint vCaster;  // the object drawn, so each transparent caster draws its own dither

void main() {
    vUV     = materialUV(aUV);
#ifndef SKINNED
    // Box-mapped, as the forward pass reads it, so the shadow is cut where the surface is.
    if (u_material.boxMapping > 0.0) {
        vec3 faceTangent;
        vUV = materialUV(boxUV(instanceModel(), aPos, aNormal, faceTangent));
    }
#endif
    vCaster = instanceObject();
    gl_Position = u_lightVP * instanceWorldPosition();
}
