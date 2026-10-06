/*
 * Splash fragment: one logo, centred over black.
 *
 * The draw covers the screen, so it is also the clear. u_rect maps the logo into the triangle's
 * 0..1 space (xy = origin, zw = size), keeping its proportions on any surface. The image is a white
 * mark on transparency, so its alpha is the coverage and u_opacity the fade.
 */

#include "../color.glsl"

in vec2 vUV;

out vec4 FragColor;

layout(binding = SPLASH_SLOT_LOGO) uniform sampler2D u_logo;
uniform vec4  u_rect;
uniform float u_opacity;
uniform int   u_hasLogo;

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

    // Sampled linear from an sRGB texture, so encoded for the 8-bit backbuffer. The fade scales
    // the encoded value, so a linear ramp is linear on the glass.
    FragColor = vec4(linearToSrgb(mark) * u_opacity, 1.0);
}
