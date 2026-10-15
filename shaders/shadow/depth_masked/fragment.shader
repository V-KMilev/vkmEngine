// A caster cut by its alpha (alpha times the albedo map's): an alpha-masked one where the forward
// pass's cutoff removes it, so a leaf card's shadow is the leaf; a transparent one against a dither,
// so it casts a share of texels as large as it is opaque and its see-through parts none. How
// smoothly that share reads as a shade is the filter's: wide where the shadow is soft, grain where
// a hard edge's few taps meet it.
#include "../../surface.glsl"
#include "../../noise.glsl"

// From this tile's texels to the light's fixed grid (GLShadowPass), so a cascade snapping along
// with the camera keeps each world texel's dither.
uniform vec2 u_ditherShift;

in vec2 vUV;
flat in uint vCaster;

void main() {
    const float a = sampledOpacity(vUV);
    // Each caster its own pattern, so two half-clear panes cast three quarters, not a half.
    vec2  layer  = fract(vec2(0.7548777, 0.5698403) * float(vCaster % 1024u)) * 64.0;
    float dither = effectNoise(gl_FragCoord.xy + u_ditherShift + layer, NOISE_SHADOW);
    if (a < (u_material.type == MAT_TRANSPARENT ? max(dither, 1e-3) : u_material.alphaCutoff)) discard;
}
