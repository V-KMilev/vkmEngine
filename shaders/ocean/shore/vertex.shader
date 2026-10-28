// The scene seen from above the ocean, for its shore (GLOceanShore::capture): world points.
// SKINNED (ocean/shore_skinned) poses each first, by the camera path's expression.
layout(location = ATTR_POSITION) in vec3 aPos;
#include "../../instancing.glsl"

uniform mat4 u_shoreVP;  // looking straight down over the shore's square

out vec3 vWorld;

void main() {
    const vec4 world = instanceWorldPosition();
    vWorld      = world.xyz;
    gl_Position = u_shoreVP * world;
}
