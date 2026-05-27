/**
 * Hi-Z reduce: max(2x2) of the previous mip into this mip.
 *
 * Conservative-far: stores the FARTHEST depth in each 2x2 footprint so
 * a future occlusion test ("is my AABB nearer than this pixel?") never
 * culls something visible. Mirrored Nyquist-conservative variant of
 * GPU-Driven Rendering (Wihlidal 2015) - the only correctness bit is
 * "max", not "average".
 *
 * The destination mip viewport is half the source dimensions; vUV is in
 * destination space [0,1]. textureGather pulls the four texels that
 * cover the corresponding 2x2 source quad in one fetch.
 */
#version 420 core

in vec2 vUV;
out float FragDistance;

uniform sampler2D u_src;     // Previous mip level.
uniform float     u_srcLod;  // textureLod argument for sampling.

void main() {
    vec2 srcSize = vec2(textureSize(u_src, int(u_srcLod)));
    vec2 texel   = 1.0 / srcSize;
    // Center of the 2x2 source footprint - textureLod with Nearest filter
    // picks one texel, so we sample four explicit offsets.
    vec2 uv = vUV;
    float a = textureLod(u_src, uv + vec2(-0.5, -0.5) * texel, u_srcLod).r;
    float b = textureLod(u_src, uv + vec2( 0.5, -0.5) * texel, u_srcLod).r;
    float c = textureLod(u_src, uv + vec2(-0.5,  0.5) * texel, u_srcLod).r;
    float d = textureLod(u_src, uv + vec2( 0.5,  0.5) * texel, u_srcLod).r;
    FragDistance = max(max(a, b), max(c, d));
}
