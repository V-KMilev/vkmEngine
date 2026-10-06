/*
 * The ground grid's quad at y = 0, recentred on the camera and scaled to cover the view; the
 * fragment shader draws the cells.
 */
layout(location = ATTR_POSITION) in vec3 aPos;   // unit quad in XZ (x,z in [-1,1], y = 0)

#include "../camera.glsl"

uniform float u_extent;   // quad half-size in world units

out vec3 vWorld;

void main() {
    vec3 eye   = u_camera.cameraPosition.xyz;
    vec3 world = vec3(eye.x + aPos.x * u_extent, 0.0, eye.z + aPos.z * u_extent);
    vWorld = world;
    gl_Position = u_camera.viewProjection * vec4(world, 1.0);
}
