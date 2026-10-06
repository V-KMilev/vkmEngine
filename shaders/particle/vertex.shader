/**
 * Billboard particle - vertex stage.
 *
 * Attribute-less: the corner comes from gl_VertexID (a 4-vertex strip), the particle from an SSBO
 * by gl_InstanceID. The quad is built on the camera's right/up axes, so it faces the view.
 */
struct Particle {
    vec4 positionSize;  // xyz = world position, w = world-space size
    vec4 color;
    vec4 params;        // x = edge softness (0 hard .. 1 soft), yzw reserved
};

layout(std430, binding = SSBO_PARTICLES) readonly buffer ParticleBlock {
    Particle particles[];
} u_particles;

#include "../camera.glsl"

out vec2  vCorner;
out vec4  vColor;
out float vSoftness;

void main() {
    Particle p = u_particles.particles[gl_InstanceID];

    // Triangle-strip corners: (-1,-1), (1,-1), (-1,1), (1,1).
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vCorner   = corner;
    vColor    = p.color;
    vSoftness = p.params.x;

    // The view matrix's rows are the camera's right and up in world space.
    vec3 camRight = vec3(u_camera.view[0][0], u_camera.view[1][0], u_camera.view[2][0]);
    vec3 camUp    = vec3(u_camera.view[0][1], u_camera.view[1][1], u_camera.view[2][1]);
    vec3 world = p.positionSize.xyz + (camRight * corner.x + camUp * corner.y) * p.positionSize.w;
    gl_Position = u_camera.viewProjection * vec4(world, 1.0);
}
