/**
 * Debug lines - fragment stage.
 *
 * Each pixel measures its distance to the segment, which makes the line smooth-edged, round at
 * its ends and as wide as asked, then takes the line's depth at its nearest point - perspective
 * correct, from the ends' - against the scene's. Seen, the line draws full; hidden, as its mode
 * says: a faint dotted ghost, nothing, or full on top.
 *
 * It blends display colors over the tonemapped frame, as the grid does.
 */
flat in vec2  vA;
flat in vec2  vB;
flat in vec2  vDepth;
flat in float vHalfWidth;
flat in float vThin;
flat in vec4  vColor;
flat in uint  vFlags;

out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // geometry target depth

#include "../../include/camera.glsl"
#include "../../include/depth.glsl"

uniform vec2  u_targetSize;    // the pixels drawn into
uniform vec2  u_targetOrigin;  // where they start in the framebuffer's

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

    // The line's depth at its point nearest the pixel: in perspective 1/depth runs straight across
    // the screen.
    float depth = cameraIsPerspective()
        ? 1.0 / mix(1.0 / vDepth.x, 1.0 / vDepth.y, t)
        : mix(vDepth.x, vDepth.y, t);
    ivec2 texel = ivec2(p / u_targetSize * u_camera.viewport);
    float scene = linearSceneDepth(texelFetch(u_sceneDepth, texel, 0).r, u_camera.invProjection);
    // Taken before anything branches, as a derivative needs its whole quad. A line in a crease
    // sits behind the surface a pixel off it, by that surface's slope.
    float slack = scene * DEPTH_SLACK + min(fwidth(scene), scene * SLOPE_SLACK);

    float alpha = vColor.a * vThin * clamp(vHalfWidth + 0.5 - across, 0.0, 1.0);

    uint mode = vFlags & 3u;
    if (mode != DEBUG_ON_TOP && depth > scene + slack) {
        if (mode == DEBUG_GHOSTED) {
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

    if (alpha <= 0.0) discard;
    FragColor = vec4(vColor.rgb, alpha);
}
