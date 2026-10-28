/*
 * The loading screen's bar (GLLoadingBarPass): a line of water running along the part filled, calm
 * along the rest, eased toward the progress. Over the loading screen's backdrop and a project's own
 * screen.
 */

#include "../../constants.glsl"
#include "../../color.glsl"

in vec2 vUV;

out vec4 FragColor;

uniform float u_opacity;  // the screen's, times how far the bar has come in
uniform float u_time;     // seconds since the screen came up
uniform vec2  u_size;     // the screen, in pixels
uniform vec4  u_bar;      // the bar's band: its centre from the bottom left and its half size, in pixels
uniform float u_fill;     // how far the bar is filled, 0..1
uniform float u_scale;    // pixels to a reference pixel

// The bar's line: its half thickness and its glow's reach where filled, in reference pixels; its
// tone where filled and where not; its wave's length, in reference pixels, how high it rises where
// filled and where not, in shares of the band's half height, and how fast it runs, a second.
const float LINE_WIDTH  = 1.1;
const float LINE_GLOW   = 4.0;
const float FILL_TONE   = 0.85;
const float TRACK_TONE  = 0.16;
const float WAVE_LENGTH = 46.0;
const float WAVE_HIGH   = 0.6;
const float WAVE_LOW    = 0.18;
const float WAVE_PACE   = 2.4;

// x squared.
float square(float x) {
    return x * x;
}

// How much of the bar's line covers the pixel @p px: a wave running along the part filled, calm
// along the rest, flattening into its ends, anti-aliased by the distance to the curve.
float barAt(vec2 px) {
    const vec2  local = px - u_bar.xy;
    const float along = local.x / u_bar.z * 0.5 + 0.5;
    if (along < 0.0 || along > 1.0) return 0.0;
    const float edge   = 6.0 * u_scale;
    const float front  = u_fill * 2.0 * u_bar.z - u_bar.z;
    const float filled = 1.0 - smoothstep(front - edge, front + edge, local.x);
    const float ends   = smoothstep(0.0, 0.06, along) * smoothstep(1.0, 0.94, along);
    const float rise   = u_bar.w * mix(WAVE_LOW, WAVE_HIGH, filled) * ends;
    // Two waves of their own lengths and paces, so it rises and falls as water does, not as a sine.
    const float k      = 2.0 * PI / (WAVE_LENGTH * u_scale);
    const float a      = k * local.x - WAVE_PACE * u_time;
    const float b      = 1.83 * k * local.x - 1.37 * WAVE_PACE * u_time + 1.1;
    const float y      = rise * (0.7 * sin(a) + 0.3 * sin(b));
    const float slope  = rise * k * (0.7 * cos(a) + 0.3 * 1.83 * cos(b));
    const float off    = abs(local.y - y) / sqrt(1.0 + slope * slope);
    const float thick  = LINE_WIDTH * u_scale;
    const float line   = 1.0 - smoothstep(thick - 0.75, thick + 0.75, off);
    const float glow   = filled * exp(-off * off / square(LINE_GLOW * u_scale));
    return line * mix(TRACK_TONE, FILL_TONE, filled) + 0.35 * glow;
}

void main() {
    const float bar = clamp(barAt(vUV * u_size), 0.0, 1.0);
    if (bar <= 0.0) discard;
    // Its tones are the light it adds over dark water, and the blend mixes what is shown: their
    // shown share, so it reads alike over any backdrop.
    FragColor = vec4(1.0, 1.0, 1.0, linearToSrgb(vec3(bar)).x * u_opacity);
}
