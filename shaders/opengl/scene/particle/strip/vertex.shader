/**
 * Particle strip - vertex stage: a trail behind a particle.
 *
 * Attribute-less: two vertices a point, gl_VertexID / 2 the point in the frame's list - one draw
 * of many strips starts each at its first - and its parity the side. Each point is widened across
 * the strip as the view sees it, so the strip faces the eye along its whole length. An alpha strip
 * is lit as a card at each point (shaders/opengl/scene/particle/light.glsl): it has no ball's form to turn to
 * the light.
 */
struct StripPoint {
    vec4 positionWidth;  // xyz = world position, w = half the strip's width there
    vec4 color;
    vec4 params;         // x = how far along the strip, 0 at its head to 1 at its tail; y, z = its
                         // strip's first and last point
    vec4 look;           // x = its falloff across, y = emission or bend, z = 1 on the viewmodel layer
};

layout(std430, binding = SSBO_STRIP_POINTS) readonly buffer StripBlock {
    StripPoint points[];
} u_strip;

#include "../../../include/camera.glsl"
#include "../../../include/constants.glsl"
#include "../../../include/lights.glsl"
#include "../../../include/cluster_grid.glsl"
#include "../../../include/shadows.glsl"
#include "../../../include/ambient.glsl"
#include "../light.glsl"

uniform int u_blend;       // PARTICLE_*: only an alpha one is lit

out vec2  vUV;         // x along the strip, y across it, 0 to 1
out vec4  vColor;
out float vSize;       // half its width here, how deep its fade runs
out vec3  vLight;      // what lights it, over its color; 1 for one that is not lit
out vec2  vAcross;     // its across in the view's right and up, which a bend follows
flat out vec3 vLook;   // StripPoint.look: falloff, emission or bend, viewmodel

void main() {
    int index = gl_VertexID / 2;
    StripPoint p    = u_strip.points[index];
    StripPoint prev = u_strip.points[max(index - 1, int(p.params.y))];
    StripPoint next = u_strip.points[min(index + 1, int(p.params.z))];

    vec3 right = cameraRight();
    vec3 up    = cameraUp();

    vec3 here   = p.positionWidth.xyz;
    vec3 along  = next.positionWidth.xyz - prev.positionWidth.xyz;
    vec3 across = cross(along, toViewer(here));
    float span  = length(across);
    // Seen end on, it has no across: any line square to the view.
    across = span > 1e-6 ? across / span : right;

    float side = float(gl_VertexID & 1) * 2.0 - 1.0;
    vec3 world = here + across * side * p.positionWidth.w;
    vUV     = vec2(p.params.x, side * 0.5 + 0.5);
    vColor  = p.color;
    vSize   = p.positionWidth.w;
    vAcross = vec2(dot(across, right), dot(across, up));
    vLook   = p.look.xyz;

    gl_Position = clipOnLayer(p.look.z > 0.5, vec4(world, 1.0));

    vLight = vec3(1.0);
    if (u_blend == PARTICLE_ALPHA) vLight = particleCardLightAt(here);
}
