/**
 * Billboard particle - fragment stage.
 *
 * A procedural soft round sprite, or a frame of the emitter's texture, tinted by the particle's
 * color. An additive particle gives off that color. An alpha one is lit as a ball: a sphere's
 * normal under the card - or its normal map's folds - turns toward or away from the light at its
 * centre, with the ambient from all round, and it gives off the share of its color its emission
 * says. A sheet's frames fade one into the next when the emitter asks. A distortion one bends
 * the scene behind it - outward from its middle, or as its texture says, read as a normal map.
 */

#include "../camera.glsl"
#include "shade.glsl"

layout(binding = PARTICLE_SLOT_NORMAL) uniform sampler2D u_normalMap;  // read while u_normalMapped

uniform int u_normalMapped;  // 1 while an alpha run lights its puffs by a normal map

in vec2  vCorner;
flat in vec2 vAxisX;
flat in vec2 vAxisY;
in vec2  vUV;
in vec2  vUVNext;
flat in vec4 vFrame;
flat in vec3 vNext;
in vec4  vColor;
in float vSoftness;
in float vSize;
flat in int vViewmodel;
flat in float vExtra;
flat in vec3 vAmbient;
flat in vec3 vLightColor;
flat in vec3 vLightDir;

// @p sheet at @p uv, held half a texel of the coarser level read inside the frame at @p corner (of
// vFrame's size), so a filtered read takes nothing of the frame beside it - down to the level whose
// texels straddle two frames, which hold both already.
vec4 frameRead(sampler2D sheet, vec2 uv, vec2 corner) {
    vec2 inset = 0.5 * exp2(ceil(textureQueryLod(sheet, uv).x)) / vec2(textureSize(sheet, 0));
    inset = min(inset, vFrame.zw * 0.5);
    return texture(sheet, clamp(uv, corner + inset, corner + vFrame.zw - inset));
}

// @p sheet where the particle is in it: this frame, faded into the next as far as it has gone.
vec4 sheetRead(sampler2D sheet) {
    vec4 here = frameRead(sheet, vUV, vFrame.xy);
    return vNext.z > 0.0 ? mix(here, frameRead(sheet, vUVNext, vNext.xy), vNext.z) : here;
}

void main() {
    vec4  color   = vColor;
    vec4  sprite  = vec4(1.0);
    float falloff = 1.0;
    float r       = length(vCorner);
    if (u_textured == 1) {
        sprite = sheetRead(u_sprite);
    } else {
        if (r > 1.0) discard;
        falloff = edgeFalloff(vSoftness, r);
    }
    falloff *= softFade(vSize, vViewmodel == 1);

    // Where on the card this is, and the card's axes, in the view's right and up.
    mat2 card   = mat2(vAxisX, vAxisY);
    vec2 facing = card * vCorner;

    if (u_blend == PARTICLE_DISTORTION) {
        // A texture is a normal map; the disc bulges out from its middle, most halfway out.
        vec2 lean = bendOf(sprite);
        vec2 bend = u_textured == 1 ? card * lean : facing * (1.0 - r) * 2.0;
        writeBent(lean, bend * vExtra * falloff * color.a, falloff * color.a);
        return;
    }

    color *= u_textured == 1 ? sprite : vec4(1.0);
    vec3 rgb = color.rgb;
    if (u_blend == PARTICLE_ALPHA) {
        // A sphere's normal under the card, in the world: across the view as the card lies, and
        // toward the eye.
        vec3  camRight = cameraRight();
        vec3  camUp    = cameraUp();
        vec3  camBack  = cameraBack();
        float reach    = length(facing);
        vec2  c        = reach > 1e-6 ? facing / reach * min(r, 1.0) : vec2(0.0);
        float toEye    = sqrt(max(1.0 - dot(c, c), 0.0));
        // A normal map's folds in place of the ball, turned with the card: x and y stored, z
        // rebuilt (TextureUsage::Normal).
        if (u_normalMapped == 1) {
            vec2 folds = sheetRead(u_normalMap).rg * 2.0 - 1.0;
            c     = card * folds;
            toEye = sqrt(max(1.0 - dot(folds, folds), 0.0));
        }
        vec3  n        = normalize(camRight * c.x + camUp * c.y + camBack * toEye);
        float wrap     = 0.5 + 0.5 * dot(n, vLightDir);
        rgb = color.rgb * (vAmbient + vLightColor * wrap) + color.rgb * vExtra;
    }
    writeParticle(rgb, color.a * falloff);
}
