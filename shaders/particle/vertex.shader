/**
 * Billboard particle - vertex stage.
 *
 * Attribute-less: the corner comes from gl_VertexID (a 4-vertex strip), the particle from an SSBO
 * by gl_InstanceID past the run's first. The quad is built on the camera's right/up axes, so it
 * faces the view, turned by the particle's angle; one that streaks lies along its trail as the
 * view sees it instead, from its head back to where the trail ends.
 */
struct Particle {
    vec4 positionSize;  // xyz = world position, w = half the billboard's width
    vec4 color;
    vec4 params;        // x = edge softness (0 hard .. 1 soft), y = angle, z = its texture (0 none),
                        // w = 1 on the viewmodel layer
    vec4 uvRect;        // its sheet frame: xy = the corner at (-1,-1), zw = the size
    vec4 trail;         // xyz = where it streaks to behind it, world space
};

layout(std430, binding = SSBO_PARTICLES) readonly buffer ParticleBlock {
    Particle particles[];
} u_particles;

#include "../camera.glsl"

uniform int u_firstInstance;  // the run's first particle: gl_InstanceID starts at 0 every draw

out vec2  vCorner;
out vec2  vUV;
flat out vec4 vFrame;  // the sheet frame's rect, which the fragment keeps its reads inside
out vec4  vColor;
out float vSoftness;
out float vSize;      // half the billboard's width (the authored size), how deep its fade runs
flat out int vViewmodel;  // drawn as a Viewmodel mesh is: its depth is the squashed band, so no fade

void main() {
    Particle p = u_particles.particles[u_firstInstance + gl_InstanceID];

    // Triangle-strip corners: (-1,-1), (1,-1), (-1,1), (1,1).
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vCorner   = corner;
    vUV       = p.uvRect.xy + (corner * 0.5 + 0.5) * p.uvRect.zw;
    vFrame    = p.uvRect;
    vColor    = p.color;
    vSoftness = p.params.x;
    vSize     = p.positionSize.w;

    // The view matrix's rows are the camera's right, up and back in world space.
    vec3 camRight = vec3(u_camera.view[0][0], u_camera.view[1][0], u_camera.view[2][0]);
    vec3 camUp    = vec3(u_camera.view[0][1], u_camera.view[1][1], u_camera.view[2][1]);
    vec3 camBack  = vec3(u_camera.view[0][2], u_camera.view[1][2], u_camera.view[2][2]);
    float size = p.positionSize.w;

    // The trail as the view sees it: what runs toward the eye does not lengthen the streak.
    vec3  seen   = p.trail.xyz - camBack * dot(p.trail.xyz, camBack);
    float span   = length(seen);
    vec3 world;
    if (span > 1e-4) {
        // Along the trail, capped by half its width at either end; corner.y = 1, the top of its
        // frame, leads.
        vec3 along  = seen / span;
        vec3 across = normalize(cross(camBack, along));
        vec3 middle = p.positionSize.xyz + p.trail.xyz * 0.5;
        world = middle + across * (corner.x * size) + along * (-corner.y * (span * 0.5 + size));
    } else {
        float c = cos(p.params.y);
        float s = sin(p.params.y);
        vec2 turned = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);
        world = p.positionSize.xyz + (camRight * turned.x + camUp * turned.y) * size;
    }
    // On the viewmodel layer, projected as shaders/viewmodel.glsl's eyeClip projects the weapon.
    vViewmodel = p.params.w > 0.5 ? 1 : 0;
    if (vViewmodel == 1) {
        gl_Position = intoViewmodelBand(u_camera.viewmodelViewProjection * vec4(world, 1.0));
    } else {
        gl_Position = u_camera.viewProjection * vec4(world, 1.0);
    }
}
