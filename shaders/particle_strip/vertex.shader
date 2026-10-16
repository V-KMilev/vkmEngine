/**
 * Particle strip - vertex stage: a ribbon through an emitter's particles, or a trail behind one.
 *
 * Attribute-less: two vertices a point, gl_VertexID / 2 the point in the frame's list - one draw
 * of many strips starts each at its first - and its parity the side. Each point is widened across
 * the strip as the view sees it, so the strip faces the eye along its whole length. An alpha strip
 * is lit as a card at each point (shaders/particle/light.glsl): it has no ball's form to turn to
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

#include "../camera.glsl"
#include "../constants.glsl"
#include "../area_light.glsl"
#include "../shadows.glsl"
#include "../ambient.glsl"
#include "../particle/light.glsl"

uniform int u_blend;       // PARTICLE_*: only an alpha one is lit
uniform vec4 u_clipPlane;  // a planar reflection's plane, while GL_CLIP_DISTANCE0 is on

out vec2  vUV;         // x along the strip, y across it, 0 to 1
out vec4  vColor;
out float vSize;       // half its width here, how deep its fade runs
out vec3  vLight;      // what lights it, over its colour; 1 for one that is not lit
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

    if (p.look.z > 0.5) {
        gl_Position = intoViewmodelBand(u_camera.viewmodelViewProjection * vec4(world, 1.0));
    } else {
        gl_Position = u_camera.viewProjection * vec4(world, 1.0);
    }
    gl_ClipDistance[0] = dot(vec4(world, 1.0), u_clipPlane);

    vLight = vec3(1.0);
    if (u_blend == PARTICLE_ALPHA) vLight = particleCardLightAt(here);
}
