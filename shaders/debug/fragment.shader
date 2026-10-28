/**
 * Debug lines - fragment stage.
 *
 * Each pixel measures its distance to the segment, which makes the line smooth-edged, round at
 * its ends and as wide as asked, then takes the line's depth at its nearest point - perspective
 * correct, from the ends' - against the scene's. Seen, the line draws full; hidden, as its mode
 * says: a faint dotted ghost, nothing, or full on top. A pulsed line lights the dashes running
 * along it from u_seconds.
 *
 * The lines stage blends display colors over the tonemapped frame, as the grid does. The glow
 * stage adds a glowing line's light into the scene's color where it is seen: its brightness on
 * the line, falling away over a few widths, which the line drawn over it keeps to a halo and the
 * bloom spreads further where it is bright.
 */
flat in vec2  vA;
flat in vec2  vB;
flat in vec2  vDepth;
flat in vec2  vArc;
flat in float vHalfWidth;
flat in float vReach;
flat in float vThin;
flat in vec4  vColor;
flat in vec4  vPulse;
flat in uint  vFlags;
flat in float vGlow;

out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // geometry target depth

#include "../camera.glsl"
#include "../depth.glsl"
#include "../color.glsl"

uniform vec2  u_targetSize;    // the pixels drawn into
uniform vec2  u_targetOrigin;  // where they start in the framebuffer's
uniform float u_seconds;       // the pulses' clock
uniform bool  u_glowStage;
uniform float u_unexposed;     // 2^-exposure: the glow's displayed brightness in the scene's units

const float DEPTH_SLACK  = 2.0e-3;  // relative: a line on a surface is seen
const float SLOPE_SLACK  = 0.05;    // the most a steep surface's own slope adds to it, relative
const float GHOST_ALPHA  = 0.35;    // a hidden line's dots, against its color
const float GHOST_PERIOD = 7.0;     // pixels from one dot to the next, at the thinnest

void main() {
    vec2  p      = gl_FragCoord.xy - u_targetOrigin;
    vec2  ab     = vB - vA;
    float span2  = dot(ab, ab);
    float t      = span2 > 1e-6 ? clamp(dot(p - vA, ab) / span2, 0.0, 1.0) : 0.0;
    float across = length(p - (vA + ab * t));

    // The line's depth and distance along its path at its point nearest the pixel: in perspective
    // 1/depth and distance/depth run straight across the screen.
    float depth, arc;
    if (cameraIsPerspective()) {
        depth = 1.0 / mix(1.0 / vDepth.x, 1.0 / vDepth.y, t);
        arc   = mix(vArc.x / vDepth.x, vArc.y / vDepth.y, t) * depth;
    } else {
        depth = mix(vDepth.x, vDepth.y, t);
        arc   = mix(vArc.x, vArc.y, t);
    }
    ivec2 texel = ivec2(p / u_targetSize * u_camera.viewport);
    float scene = linearSceneDepth(texelFetch(u_sceneDepth, texel, 0).r, u_camera.invProjection);
    // Taken before anything branches, as a derivative needs its whole quad. A line in a crease
    // sits behind the surface a pixel off it, by that surface's slope.
    float arcPerPixel = fwidth(arc);
    float slack       = scene * DEPTH_SLACK + min(fwidth(scene), scene * SLOPE_SLACK);

    // The pixel's share of the line, or of its glow: brightest on it, falling away over its reach.
    float shape = u_glowStage
        ? pow(1.0 - clamp(across / vReach, 0.0, 1.0), 3.0)
        : clamp(vHalfWidth + 0.5 - across, 0.0, 1.0);
    float alpha = vColor.a * vThin * shape;

    uint mode = vFlags & 3u;
    if ((mode != DEBUG_ON_TOP || u_glowStage) && depth > scene + slack) {
        if (mode == DEBUG_GHOSTED && !u_glowStage) {
            // Round dots down the line, faint, one on its start: a point, or a line too short for
            // a period, is a dot.
            float period = max(GHOST_PERIOD, vHalfWidth * 4.0);
            float along  = mod(t * sqrt(span2) + period * 0.5, period) - period * 0.5;
            float dotRadius = max(vHalfWidth, 1.0);
            float coverage  = clamp(dotRadius + 0.5 - length(vec2(along, across)), 0.0, 1.0);
            alpha = vColor.a * vThin * GHOST_ALPHA * coverage;
        } else {
            alpha = 0.0;
        }
    }

    if (vPulse.y > 0.0) {
        // How far the nearest head ahead is: lit within a dash of it, fading to the tail, its
        // leading edge a pixel soft.
        float behind = mod(u_seconds * vPulse.z + vPulse.w - arc, vPulse.y);
        float lit    = behind < vPulse.x ? 1.0 - behind / vPulse.x : 0.0;
        lit *= clamp(behind / max(arcPerPixel, 1e-5), 0.0, 1.0);
        float trail = float((vFlags >> 8u) & 255u) / 255.0;
        alpha *= max(lit, trail);
    }

    if (alpha <= 0.0) discard;
    if (u_glowStage) {
        FragColor = vec4(srgbToLinear(vColor.rgb) * (vGlow * u_unexposed * alpha), 0.0);
    } else {
        FragColor = vec4(vColor.rgb, alpha);
    }
}
