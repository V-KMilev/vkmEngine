/*
 * UI fragment. Two kinds share one program:
 *   0 Solid - flat tint, sampler ignored.
 *   1 Text  - signed-distance glyph: the atlas stores distance to the edge, and a
 *             screen-space derivative gives resolution-independent anti-aliasing,
 *             so one bake stays crisp at any size.
 * Both paths alpha-blend over the composited scene.
 */

in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D u_tex;
uniform int       u_kind;

void main() {
    if (u_kind == 1) {
        float dist  = texture(u_tex, vUV).r;
        float aa    = fwidth(dist);
        float cover = smoothstep(0.5 - aa, 0.5 + aa, dist);
        FragColor = vec4(vColor.rgb, vColor.a * cover);
    } else {
        FragColor = vColor;
    }
}
