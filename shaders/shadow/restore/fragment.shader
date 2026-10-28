/**
 * A shadow tile started from the layer (GLShadowPass, Shadow2DJob::layered): the depth the layer
 * holds of the tile's fixed casters, texel for texel, which the moving ones then draw over.
 */

layout(binding = SHADOW_SLOT_LAYER) uniform sampler2D u_heldDepth;
uniform ivec2 u_shift;  // from a texel of the atlas to the same one of the layer

void main() {
    gl_FragDepth = texelFetch(u_heldDepth, ivec2(gl_FragCoord.xy) + u_shift, 0).r;
}
