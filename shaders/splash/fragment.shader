/*
 * Splash fragment: one logo, centred over black.
 *
 * The draw covers the whole screen and is therefore also the clear: a splash is
 * black everywhere the logo is not. u_rect maps the logo into the fullscreen
 * triangle's 0..1 space (xy = origin, zw = size), computed on the CPU from the
 * image's aspect against the window's so the mark keeps its proportions on any
 * surface. Outside that rect the sampler is never read; inside, the image is a
 * white mark on transparency, so its alpha is the coverage and u_opacity is the
 * fade.
 */

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_logo;
uniform vec4      u_rect;
uniform float     u_opacity;
uniform int       u_hasLogo;

void main() {
    // Nothing decoded: the ground alone. Sampling anyway reads whatever texture
    // unit 0 happens to hold, which is a glyph atlas as often as not.
    if (u_hasLogo == 0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // No flip: the loader asks stb for bottom-up rows, which is the order the
    // triangle's v already runs in.
    vec2 uv = (vUV - u_rect.xy) / u_rect.zw;

    vec3 mark = vec3(0.0);
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
        vec4 texel = texture(u_logo, uv);
        mark = texel.rgb * texel.a;
    }

    FragColor = vec4(mark * u_opacity, 1.0);
}
