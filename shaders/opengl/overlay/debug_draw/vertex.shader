/**
 * Debug lines - vertex stage.
 *
 * Attribute-less: an instance is one DebugLine (debug/draw/debug_line.h), the corner
 * gl_VertexID of a 4-vertex strip. The segment is cut at the near plane, so a line running past
 * the eye stays on screen, then drawn as a quad round its ends in the target's pixels: half its
 * width and a pixel more out on every side, which the fragment stage's coverage and round caps
 * fill. Its ends and depths go flat to the fragment stage, which measures each pixel against the
 * segment itself.
 */
struct DebugLine {
    vec3  a;      // its start
    float width;  // points
    vec3  b;      // its end; an end on the start is a round dot
    uint  color;  // display RGBA8, red in the low byte
    uint  flags;  // DEBUG_* depth mode in bits 0-1
};

layout(std430, binding = SSBO_DEBUG_LINES) readonly buffer DebugLineBlock {
    DebugLine lines[];
} u_debug;

#include "../../include/camera.glsl"

uniform vec2  u_targetSize;      // the pixels drawn into
uniform float u_pixelsPerPoint;

flat out vec2  vA;          // the ends as drawn, target pixels
flat out vec2  vB;
flat out vec2  vDepth;      // their view depths
flat out float vHalfWidth;  // pixels
flat out float vThin;       // a line under a pixel is drawn a pixel wide, this faint
flat out vec4  vColor;
flat out uint  vFlags;

// Past the far corner of clip space: the four corners of a line not drawn fold into nothing there.
const vec4 NOWHERE = vec4(2.0, 2.0, 2.0, 1.0);

void main() {
    DebugLine line = u_debug.lines[gl_InstanceID];
    vec3  va = (u_camera.view * vec4(line.a, 1.0)).xyz;
    vec3  vb = (u_camera.view * vec4(line.b, 1.0)).xyz;

    // Cut at the near plane, view z = -zNear.
    float nearZ   = -u_camera.zNear;
    bool  aBehind = va.z > nearZ;
    bool  bBehind = vb.z > nearZ;
    if (aBehind && bBehind) {
        gl_Position = NOWHERE;
        return;
    }
    if (aBehind) {
        float t = (nearZ - va.z) / (vb.z - va.z);
        va = mix(va, vb, t);
    } else if (bBehind) {
        float t = (nearZ - vb.z) / (va.z - vb.z);
        vb = mix(vb, va, t);
    }

    vec4 ca = u_camera.projection * vec4(va, 1.0);
    vec4 cb = u_camera.projection * vec4(vb, 1.0);
    vec2 pa = (ca.xy / ca.w * 0.5 + 0.5) * u_targetSize;
    vec2 pb = (cb.xy / cb.w * 0.5 + 0.5) * u_targetSize;

    float width  = line.width * u_pixelsPerPoint;
    float radius = max(width, 1.0) * 0.5;
    float reach  = radius + 1.0;

    vec2  d      = pb - pa;
    float span   = length(d);
    vec2  along  = span > 1e-3 ? d / span : vec2(1.0, 0.0);
    vec2  across = vec2(-along.y, along.x);
    bool  atB    = gl_VertexID >= 2;
    float side   = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
    vec2  corner = (atB ? pb : pa) + along * (atB ? reach : -reach) + across * (side * reach);
    gl_Position  = vec4(corner / u_targetSize * 2.0 - 1.0, 0.5, 1.0);

    vA         = pa;
    vB         = pb;
    vDepth     = vec2(-va.z, -vb.z);
    vHalfWidth = radius;
    vThin      = min(width, 1.0);
    vColor     = unpackUnorm4x8(line.color);
    vFlags     = line.flags;
}
