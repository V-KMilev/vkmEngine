/**
 * Billboard particle - vertex stage.
 *
 * Attribute-less: the quad corner comes from gl_VertexID (a 4-vertex triangle
 * strip) and the per-particle position/size/colour from an SSBO indexed by
 * gl_InstanceID, so there is no vertex buffer or attribute layout to maintain.
 * The quad is built on the camera's right/up axes, so it always faces the view.
 */

struct Particle {
    vec4 positionSize;  // xyz = world position, w = world-space size
    vec4 color;
    vec4 params;        // x = edge softness (0 hard .. 1 soft), yzw reserved
};

layout(std430, binding = 2) readonly buffer ParticleBlock {
    Particle particles[];
} u_particles;

uniform mat4 u_viewProj;
uniform vec3 u_camRight;
uniform vec3 u_camUp;

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

    vec3 world = p.positionSize.xyz
               + (u_camRight * corner.x + u_camUp * corner.y) * p.positionSize.w;
    gl_Position = u_viewProj * vec4(world, 1.0);
}
