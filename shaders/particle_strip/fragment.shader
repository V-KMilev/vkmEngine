/**
 * Particle strip - fragment stage: soft across its width, its texture laid along it, and
 * otherwise a particle's (shaders/particle/shade.glsl): an additive strip gives off its colour, an
 * alpha one is lit, a distortion one bends the scene behind across its width.
 */

#include "../camera.glsl"
#include "../particle/shade.glsl"

in vec2  vUV;
in vec4  vColor;
in float vSize;
in vec3  vLight;
in vec2  vAcross;
flat in vec3 vLook;

void main() {
    // Bare, it fades across its width as its softness says; a texture's own alpha does that.
    float across  = vUV.y * 2.0 - 1.0;
    float edge    = abs(across);
    float falloff = u_textured == 1 ? 1.0 : edgeFalloff(vLook.x, edge);
    falloff *= softFade(vSize, vLook.z > 0.5);
    vec4 sprite = u_textured == 1 ? texture(u_sprite, vUV) : vec4(1.0);

    if (u_blend == PARTICLE_DISTORTION) {
        // A texture is a normal map, x across and y along; bare, it bulges out from its middle
        // line. Either way in the view's right and up, the way the strip runs there.
        vec2 outward = normalize(vAcross + vec2(1e-8, 0.0));
        vec2 along   = vec2(-outward.y, outward.x);
        vec2 lean    = bendOf(sprite);
        vec2 bend    = u_textured == 1 ? mat2(outward, along) * lean : outward * across * (1.0 - edge) * 2.0;
        writeBent(lean, bend * vLook.y * falloff * vColor.a, falloff * vColor.a);
        return;
    }
    vec4 color = vColor * sprite;
    vec3 rgb   = color.rgb * vLight;
    if (u_blend == PARTICLE_ALPHA) rgb += color.rgb * vLook.y;
    writeParticle(rgb, color.a * falloff);
}
