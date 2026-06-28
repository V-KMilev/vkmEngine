/*
 * World-space ground grid: a quad in the XZ plane, recentred on the camera and
 * scaled large so it covers the view. The fragment shader draws the cells; this
 * just places the quad at y = 0 and forwards the world position.
 */

layout(location = 0) in vec3 aPos;   // unit quad in XZ (x,z in [-1,1], y = 0)

uniform mat4  u_viewProj;
uniform vec3  u_camPos;
uniform float u_extent;   // quad half-size in world units

out vec3 vWorld;

void main() {
    vec3 world = vec3(u_camPos.x + aPos.x * u_extent, 0.0, u_camPos.z + aPos.z * u_extent);
    vWorld = world;
    gl_Position = u_viewProj * vec4(world, 1.0);
}
