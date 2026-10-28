/**
 * Debug lines - vertex stage.
 *
 * Attribute-less: an instance is one DebugLine (system/render/data/debug_line_data.h), the corner
 * gl_VertexID of a 4-vertex strip. The segment is cut at the near plane, so a line running past
 * the eye stays on screen, then drawn as a quad round its ends in the target's pixels: half its
 * width and a pixel more out on every side, which the fragment stage's coverage and round caps
 * fill - in the glow stage, as far as its glow reaches. Its ends, depths and distances along the
 * path go flat to the fragment stage, which measures each pixel against the segment itself.
 */
struct DebugLine {
    vec4  a;      // xyz its start, w how far along its path, metres
    vec4  b;      // its end likewise; an end on the start is a round dot
    vec4  pulse;  // dash, spacing, speed and phase: metres, the speed metres a second; spacing 0 is solid
    uint  color;  // display RGBA8, red in the low byte
    float width;  // points
    float glow;   // brightness of its glow, displayed units
    uint  flags;  // DEBUG_* depth mode in bits 0-1, the pulse's trail in bits 8-15
};

layout(std430, binding = SSBO_DEBUG_LINES) readonly buffer DebugLineBlock {
    DebugLine lines[];
} u_debug;

#include "../camera.glsl"

uniform vec2  u_targetSize;      // the pixels drawn into
uniform float u_pixelsPerPoint;
uniform bool  u_glowStage;       // only lines that glow, as far as their glow reaches

flat out vec2  vA;          // the ends as drawn, target pixels
flat out vec2  vB;
flat out vec2  vDepth;      // their view depths
flat out vec2  vArc;        // their distances along the path
flat out float vHalfWidth;  // pixels
flat out float vReach;      // how far from the segment the quad reaches, pixels
flat out float vThin;       // a line under a pixel is drawn a pixel wide, this faint
flat out vec4  vColor;
flat out vec4  vPulse;
flat out uint  vFlags;
flat out float vGlow;

// Past the far corner of clip space: the four corners of a line not drawn fold into nothing there.
const vec4 NOWHERE = vec4(2.0, 2.0, 2.0, 1.0);

// How far a glow reaches past the line: this many of its half-widths, and these points more.
const float GLOW_WIDTHS = 3.0;
const float GLOW_POINTS = 4.0;

void main() {
    DebugLine line = u_debug.lines[gl_InstanceID];
    if (u_glowStage && line.glow <= 0.0) {
        gl_Position = NOWHERE;
        return;
    }

    vec3  va = (u_camera.view * vec4(line.a.xyz, 1.0)).xyz;
    vec3  vb = (u_camera.view * vec4(line.b.xyz, 1.0)).xyz;
    float sa = line.a.w;
    float sb = line.b.w;

    // Cut at the near plane, view z = -zNear, keeping the distances along the path in step.
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
        sa = mix(sa, sb, t);
    } else if (bBehind) {
        float t = (nearZ - vb.z) / (va.z - vb.z);
        vb = mix(vb, va, t);
        sb = mix(sb, sa, t);
    }

    vec4 ca = u_camera.projection * vec4(va, 1.0);
    vec4 cb = u_camera.projection * vec4(vb, 1.0);
    vec2 pa = (ca.xy / ca.w * 0.5 + 0.5) * u_targetSize;
    vec2 pb = (cb.xy / cb.w * 0.5 + 0.5) * u_targetSize;

    float width  = line.width * u_pixelsPerPoint;
    float radius = max(width, 1.0) * 0.5;
    float glow   = radius * GLOW_WIDTHS + GLOW_POINTS * u_pixelsPerPoint;
    float reach  = u_glowStage ? glow : radius + 1.0;

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
    vArc       = vec2(sa, sb);
    vHalfWidth = radius;
    vReach     = reach;
    vThin      = min(width, 1.0);
    vColor     = unpackUnorm4x8(line.color);
    vPulse     = line.pulse;
    vFlags     = line.flags;
    vGlow      = line.glow;
}
