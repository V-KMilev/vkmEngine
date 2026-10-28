// Depth-only shadow pass into the light's clip space; the caster's model matrix comes by instance index.
// SKINNED (shadow/depth_skinned) poses it first, by the camera path's expression.
layout(location = ATTR_POSITION) in vec3 aPos;
#include "../../instancing.glsl"

uniform mat4 u_lightVP;

void main() {
    gl_Position = u_lightVP * instanceWorldPosition();
}
