/*
 * Splash fragment: one logo, centred over black, and under it at the screen's foot the line of its
 * caption, if it has one, in grey.
 *
 * The draw covers the screen, so it is also the clear. u_rect and u_captionRect map each image into
 * the triangle's 0..1 space (xy = origin, zw = size), keeping its proportions on any surface. Each
 * is a white mark on transparency, so its alpha is the coverage and u_opacity the fade.
 */

#include "../color.glsl"

in vec2 vUV;

out vec4 FragColor;

layout(binding = SPLASH_SLOT_LOGO) uniform sampler2D u_logo;
layout(binding = SPLASH_SLOT_CAPTION) uniform sampler2D u_caption;
uniform vec4  u_rect;
uniform vec4  u_captionRect;
uniform float u_opacity;
uniform int   u_hasLogo;
uniform int   u_hasCaption;

// How bright the caption's line is beside the logo, linear.
const float CAPTION_TONE = 0.3;

void main() {
    // Nothing decoded: sampling anyway reads whatever unit 0 holds, often a glyph atlas.
    if (u_hasLogo == 0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // No flip: the loader asks stb for bottom-up rows, the order the triangle's v runs in.
    vec2 uv = (vUV - u_rect.xy) / u_rect.zw;

    vec3 mark = vec3(0.0);
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
        vec4 texel = texture(u_logo, uv);
        mark = texel.rgb * texel.a;
    }
    vec2 line = (vUV - u_captionRect.xy) / u_captionRect.zw;
    if (u_hasCaption != 0 && line.x >= 0.0 && line.x <= 1.0 && line.y >= 0.0 && line.y <= 1.0) {
        vec4 texel = texture(u_caption, line);
        mark += texel.rgb * texel.a * CAPTION_TONE;
    }

    // Sampled linear from an sRGB texture, so encoded for the 8-bit backbuffer. The fade scales
    // the encoded value, so a linear ramp is linear on the glass.
    FragColor = vec4(linearToSrgb(mark) * u_opacity, 1.0);
}
