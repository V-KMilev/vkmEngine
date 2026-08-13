/**
 * Billboard particle - fragment stage.
 *
 * A soft round sprite generated procedurally from the quad corner, so particles
 * need no texture asset. Outputs LINEAR radiance into the HDR scene; the composite
 * pass owns tonemap + gamma.
 */

in vec2  vCorner;
in vec4  vColor;
in float vSoftness;

out vec4 FragColor;

void main() {
    float r = length(vCorner);
    if (r > 1.0) discard;                    // outside the disc

    // Edge falloff width follows the emitter's softness: 1 fades over the
    // whole disc (soft blob), 0 keeps a thin anti-aliased rim (crisp disc).
    float inner   = 1.0 - max(vSoftness, 0.04);
    float falloff = smoothstep(1.0, inner, r);
    FragColor = vec4(vColor.rgb, vColor.a * falloff);
}
