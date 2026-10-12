/**
 * Billboard particle - vertex stage.
 *
 * Attribute-less: the corner comes from gl_VertexID (a 4-vertex strip), the particle from an SSBO
 * by gl_InstanceID past the run's first. The quad is built on the camera's right/up axes, so it
 * faces the view, turned by the particle's angle; one that streaks lies along its trail as the
 * view sees it instead, from its head back to where the trail ends.
 *
 * An alpha-blended particle scatters the light reaching it - smoke, dust, steam - so it is lit
 * here, its colour an albedo: as a diffuse card facing each light, through the light's shadow,
 * plus the environment's mean radiance, since a puff has no normal of its own. An additive one
 * emits, so its colour is its radiance. It is lit at its centre - a streak's middle - each corner
 * taking the same light: a corner lies off the puff, past a wall or in a lamp's reach the puff is not, and
 * turns with the view. A puff wider than a shadow's edge takes the light at its middle.
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
#include "../constants.glsl"
#include "../lights.glsl"
#include "../shadows.glsl"
#include "../ambient.glsl"

uniform int u_firstInstance;  // the run's first particle: gl_InstanceID starts at 0 every draw
uniform int u_additive;       // 1 while the additive half draws: it emits, so it is not lit

out vec2  vCorner;
out vec2  vUV;
flat out vec4 vFrame;  // the sheet frame's rect, which the fragment keeps its reads inside
out vec4  vColor;
out float vSoftness;
out float vSize;      // half the billboard's width (the authored size), how deep its fade runs
flat out int vViewmodel;  // drawn as a Viewmodel mesh is: its depth is the squashed band, so no fade
out vec3  vLight;     // the radiance it scatters over its albedo; 1 for an additive one

// The environment's mean radiance at p: its irradiance over PI averaged over the six axes, which
// is what an isotropic scatterer takes from all round.
vec3 meanAmbient(vec3 p) {
    vec3 sum = environmentIrradiance(p, vec3(1.0, 0.0, 0.0)) + environmentIrradiance(p, vec3(-1.0, 0.0, 0.0))
        + environmentIrradiance(p, vec3(0.0, 1.0, 0.0)) + environmentIrradiance(p, vec3(0.0, -1.0, 0.0))
        + environmentIrradiance(p, vec3(0.0, 0.0, 1.0)) + environmentIrradiance(p, vec3(0.0, 0.0, -1.0));
    return sum / 6.0;
}

// What lights a particle at p, over its albedo: each of its cluster's lights as on a diffuse card
// facing it, through the light's hard shadow at that point, and the environment.
vec3 scatteredLight(vec3 p) {
    vec3 radiance = meanAmbient(p);

    // Its cluster is where it is in the world, whichever projection draws it.
    vec4  seen  = u_camera.viewProjection * vec4(p, 1.0);
    vec2  uv    = seen.xy / max(seen.w, 1e-4) * 0.5 + 0.5;
    float depth = max(-(u_camera.view * vec4(p, 1.0)).z, u_camera.zNear);
    int   ci    = clusterIndex(uv, depth, u_camera.zNear, u_camera.zFar);

    uint count = u_clusters.clusters[ci].count;
    for (uint k = 0u; k < count; ++k) {
        Light light     = u_lights.lights[u_clusters.clusters[ci].indices[k]];
        vec3  lightCol  = light.color.xyz * light.color.w;
        int   type      = int(light.position.w);

        // An area light as the small source it is from far enough: its point-equivalent intensity
        // over the squared distance, by how squarely its face turns to the particle.
        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            vec3  toParticle = p - light.position.xyz;
            float dist       = length(toParticle);
            float window     = distanceWindow(dist, light.direction.w);
            vec3  face       = normalize(cross(light.axisU.xyz, light.axisV.xyz));
            float facing     = dot(toParticle / max(dist, 1e-4), face);
            if (light.axisU.w > 0.5) facing = abs(facing);
            radiance += lightCol * window * max(facing, 0.0) / max(dist * dist, 1e-4) / PI;
            continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, p, L);
        if (atten <= 0.0) continue;
        radiance += lightCol * atten * pointShadow(type, int(light.spot.w), p) / PI;
    }
    return radiance;
}

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
    vec3 centre = p.positionSize.xyz;
    if (span > 1e-4) {
        // Along the trail, capped by half its width at either end; corner.y = 1, the top of its
        // frame, leads.
        vec3 along  = seen / span;
        vec3 across = normalize(cross(camBack, along));
        centre = p.positionSize.xyz + p.trail.xyz * 0.5;
        world  = centre + across * (corner.x * size) + along * (-corner.y * (span * 0.5 + size));
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

    vLight = u_additive == 1 ? vec3(1.0) : scatteredLight(centre);
}
