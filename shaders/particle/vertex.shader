/**
 * Billboard particle - vertex stage.
 *
 * Attribute-less: the corner comes from gl_VertexID (a 4-vertex strip), the particle from an SSBO
 * by gl_InstanceID past the run's first. The quad is built on the camera's right/up axes, so it
 * faces the view, turned by the particle's angle; one that streaks lies along its trail as the
 * view sees it instead, from its head back to where the trail ends.
 *
 * An alpha-blended particle scatters the light reaching it - smoke, dust, steam - so its colour is
 * an albedo, lit as a ball: the light at its centre (shaders/particle/light.glsl) goes to the
 * fragment stage, which turns a sphere's normal to it. Every corner takes the light of its
 * centre: a corner lies off the puff, past a wall or in a lamp's reach the puff is not, and turns
 * with the view.
 */
struct Particle {
    vec4 positionSize;  // xyz = world position, w = half the billboard's width
    vec4 color;
    vec4 params;        // x = edge softness (0 hard .. 1 soft), y = angle, z = its texture (0 none),
                        // w = 1 on the viewmodel layer
    vec4 uvRect;        // its sheet frame: xy = the corner at (-1,-1), zw = the size
    vec4 trail;         // xyz = where it streaks to behind it, world space; w = emission or bend
    vec4 uvNext;        // xy = the next sheet frame's corner, z = how far it faded into it,
                        // w = its normal map (0 none)
};

layout(std430, binding = SSBO_PARTICLES) readonly buffer ParticleBlock {
    Particle particles[];
} u_particles;

#include "../camera.glsl"
#include "../constants.glsl"
#include "../area_light.glsl"
#include "../shadows.glsl"
#include "../ambient.glsl"
#include "light.glsl"

uniform int u_firstInstance;  // the run's first particle: gl_InstanceID starts at 0 every draw
uniform int u_blend;          // PARTICLE_*: only an alpha one is lit

out vec2  vCorner;
flat out vec2 vAxisX;  // the card's own x, then y, in the view's right and up: turned, or streaked
flat out vec2 vAxisY;
out vec2  vUV;
out vec2  vUVNext;     // the same corner of the next frame
flat out vec4 vFrame;  // the sheet frame's rect, which the fragment keeps its reads inside
flat out vec3 vNext;   // xy = the next frame's corner, which its reads keep inside; z = the fade
out vec4  vColor;
out float vSoftness;
out float vSize;       // half the billboard's width, how deep its fade runs
flat out int vViewmodel;  // drawn as a Viewmodel mesh is: its depth is the squashed band, so no fade
flat out float vExtra;    // the share of its colour it gives off, or how far it bends the view
flat out vec3 vAmbient;   // the light at its centre, for an alpha one
flat out vec3 vLightColor;
flat out vec3 vLightDir;

void main() {
    Particle p = u_particles.particles[u_firstInstance + gl_InstanceID];

    // Triangle-strip corners: (-1,-1), (1,-1), (-1,1), (1,1).
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vCorner   = corner;
    vUV       = p.uvRect.xy + (corner * 0.5 + 0.5) * p.uvRect.zw;
    vUVNext   = p.uvNext.xy + (corner * 0.5 + 0.5) * p.uvRect.zw;
    vNext     = p.uvNext.xyz;
    vFrame    = p.uvRect;
    vColor    = p.color;
    vSoftness = p.params.x;
    vSize     = p.positionSize.w;
    vExtra    = p.trail.w;

    vec3 camRight = cameraRight();
    vec3 camUp    = cameraUp();
    vec3 camBack  = cameraBack();
    float size = p.positionSize.w;

    // The trail as the view sees it: what runs toward the eye does not lengthen the streak.
    vec3  seen   = p.trail.xyz - camBack * dot(p.trail.xyz, camBack);
    float span   = length(seen);
    vec3 world;
    vec3 centre = p.positionSize.xyz;
    if (span > 1e-4) {
        // Along the trail, capped by half its width at either end; corner.y = 1, the top of its
        // frame, leads.
        vec3 along  = seen / span;
        vec3 across = normalize(cross(camBack, along));
        centre = p.positionSize.xyz + p.trail.xyz * 0.5;
        world  = centre + across * (corner.x * size) + along * (-corner.y * (span * 0.5 + size));
        vAxisX = vec2(dot(across, camRight), dot(across, camUp));
        vAxisY = -vec2(dot(along, camRight), dot(along, camUp));
    } else {
        float c = cos(p.params.y);
        float s = sin(p.params.y);
        vAxisX = vec2(c, s);
        vAxisY = vec2(-s, c);
        vec2 turned = vAxisX * corner.x + vAxisY * corner.y;
        world = p.positionSize.xyz + (camRight * turned.x + camUp * turned.y) * size;
    }
    // On the viewmodel layer, projected as shaders/viewmodel.glsl's eyeClip projects the weapon.
    vViewmodel = p.params.w > 0.5 ? 1 : 0;
    if (vViewmodel == 1) {
        gl_Position = intoViewmodelBand(u_camera.viewmodelViewProjection * vec4(world, 1.0));
    } else {
        gl_Position = u_camera.viewProjection * vec4(world, 1.0);
    }

    vAmbient    = vec3(1.0);
    vLightColor = vec3(0.0);
    vLightDir   = vec3(0.0, 1.0, 0.0);
    if (u_blend == PARTICLE_ALPHA) {
        ParticleLight lit = particleLightAt(centre);
        vAmbient    = lit.ambient;
        vLightColor = lit.color;
        vLightDir   = lit.direction;
    }
}
