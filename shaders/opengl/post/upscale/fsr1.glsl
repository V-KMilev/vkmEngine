/*
 * AMD FidelityFX Super Resolution 1: EASU, an edge-adaptive spatial upscale, and RCAS, the
 * contrast-adaptive sharpen run on its output. A port of ffx_fsr1.h (v1.20210629), its 32-bit paths:
 *
 *   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies or
 *   substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 *   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   (The same notice is in LICENSE-MIT beside this file.)
 *
 * Changed from the original: GLSL alone, without ffx_a.h, whose approximations used here are
 * written out; FsrEasuCon's constants worked out from the two sizes; the source a sampler argument
 * in place of input callbacks; each EASU corner's bilinear weight passed in; RCAS without its
 * noise filter, which only FSR_RCAS_DENOISE reads, and reading clamped to the image's edge.
 *
 * Both want a display-encoded image in 0..1, after the tonemap and before film grain or the UI.
 */

// ffx_a.h's approximations from a float's bits: a low-precision reciprocal and reciprocal square
// root, and a medium-precision reciprocal refined by one Newton step.
float fsrRcpLow(float a) {
    return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(a));
}

float fsrRsqLow(float a) {
    return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(a) >> 1u));
}

float fsrRcpMed(float a) {
    float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(a));
    return b * (-b * a + 2.0);
}

// One EASU tap of color @p c, @p off from the resolve position: the kernel rotated to @p dir,
// stretched by @p len, its negative lobe @p lob, clipped at distance squared @p clp. The weight is
// (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (lob * x^2 - 1)^2, lanczos 2 without sin, rcp or sqrt.
void fsrEasuTap(
    inout vec3 aC,
    inout float aW,
    vec2 off,
    vec2 dir,
    vec2 len,
    float lob,
    float clp,
    vec3 c
) {
    vec2 v;
    v.x = (off.x * dir.x) + (off.y * dir.y);
    v.y = (off.x * -dir.y) + (off.y * dir.x);
    v *= len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

// One of the four texels nearest the resolve position, of bilinear weight @p w, adds its '+' of
// lumas (a above, b c d across, e below) to the kernel's direction and its edge to the length: a
// gradient that reverses gives 0, one that does not gives 1.
void fsrEasuSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc   = lD - lC;
    float cb   = lC - lB;
    float lenX = fsrRcpLow(max(abs(dc), abs(cb)));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float ec   = lE - lC;
    float ca   = lC - lA;
    float lenY = fsrRcpLow(max(abs(ec), abs(ca)));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

// EASU: output pixel @p ip (integral, from the image's first row) of an image @p outputSize across,
// from all of @p src, @p inputSize across. 12 taps, gathered round four points:
//    b c
//  e f g h
//  i j k l
//    n o
vec3 fsrEasu(sampler2D src, vec2 ip, vec2 inputSize, vec2 outputSize) {
    vec2 scale = inputSize / outputSize;
    vec2 texel = 1.0 / inputSize;
    vec2 pp    = ip * scale + (0.5 * scale - 0.5);
    vec2 fp    = floor(pp);
    pp -= fp;

    // Each gather is {r g / a b} round its point; the unused texels are z.
    vec2 p0    = fp * texel + vec2(1.0, -1.0) * texel;
    vec2 p1    = p0 + vec2(-1.0, 2.0) * texel;
    vec2 p2    = p0 + vec2(1.0, 2.0) * texel;
    vec2 p3    = p0 + vec2(0.0, 4.0) * texel;
    vec4 bczzR = textureGather(src, p0, 0);
    vec4 bczzG = textureGather(src, p0, 1);
    vec4 bczzB = textureGather(src, p0, 2);
    vec4 ijfeR = textureGather(src, p1, 0);
    vec4 ijfeG = textureGather(src, p1, 1);
    vec4 ijfeB = textureGather(src, p1, 2);
    vec4 klhgR = textureGather(src, p2, 0);
    vec4 klhgG = textureGather(src, p2, 1);
    vec4 klhgB = textureGather(src, p2, 2);
    vec4 zzonR = textureGather(src, p3, 0);
    vec4 zzonG = textureGather(src, p3, 1);
    vec4 zzonB = textureGather(src, p3, 2);

    // Luma times two, in two multiply-adds.
    vec4  bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    vec4  ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    vec4  klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    vec4  zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
    float bL    = bczzL.x;
    float cL    = bczzL.y;
    float iL    = ijfeL.x;
    float jL    = ijfeL.y;
    float fL    = ijfeL.z;
    float eL    = ijfeL.w;
    float kL    = klhgL.x;
    float lL    = klhgL.y;
    float hL    = klhgL.z;
    float gL    = klhgL.w;
    float oL    = zzonL.z;
    float nL    = zzonL.w;

    vec2  dir = vec2(0.0);
    float len = 0.0;
    fsrEasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    fsrEasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    fsrEasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    fsrEasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

    // The direction normalized, a near-zero one taken as horizontal; the length from 0..2 to 0..1,
    // squared.
    vec2  dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool  zero = dirR < 1.0 / 32768.0;
    dirR  = zero ? 1.0 : fsrRsqLow(dirR);
    dir.x = zero ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;

    // The kernel stretched from 1 along an axis to sqrt(2) on a diagonal, and to twice as long
    // across an edge as along it; its window from +/-sqrt(2) out to a little past 2 on an edge.
    float stretch = (dir.x * dir.x + dir.y * dir.y) * fsrRcpLow(max(abs(dir.x), abs(dir.y)));
    vec2  len2    = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob     = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp     = fsrRcpLow(lob);

    // The result is held within the four nearest texels' range, so the negative lobes never ring.
    vec3 f    = vec3(ijfeR.z, ijfeG.z, ijfeB.z);
    vec3 g    = vec3(klhgR.w, klhgG.w, klhgB.w);
    vec3 j    = vec3(ijfeR.y, ijfeG.y, ijfeB.y);
    vec3 k    = vec3(klhgR.x, klhgG.x, klhgB.x);
    vec3 min4 = min(min(f, min(g, j)), k);
    vec3 max4 = max(max(f, max(g, j)), k);

    vec3  aC = vec3(0.0);
    float aW = 0.0;
    fsrEasuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));
    fsrEasuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));
    fsrEasuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));
    fsrEasuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    fsrEasuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    fsrEasuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));
    fsrEasuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    fsrEasuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));
    fsrEasuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));
    fsrEasuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    fsrEasuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));
    fsrEasuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));
    return min(max4, max(min4, aC * (1.0 / aW)));
}

// The most RCAS's negative lobe may weigh before sharpening looks unnatural.
const float FSR_RCAS_LIMIT = 0.25 - 1.0 / 16.0;

// RCAS: pixel @p ip of @p src sharpened by the most its cross of neighbors allows before a channel
// would clip, scaled by @p sharpness (1 at the most, halved a stop at a time).
vec3 fsrRcas(sampler2D src, ivec2 ip, float sharpness) {
    //    b
    //  d e f
    //    h
    ivec2 last = textureSize(src, 0) - 1;
    vec3  b    = texelFetch(src, clamp(ip + ivec2(0, -1), ivec2(0), last), 0).rgb;
    vec3  d    = texelFetch(src, clamp(ip + ivec2(-1, 0), ivec2(0), last), 0).rgb;
    vec3  e    = texelFetch(src, clamp(ip, ivec2(0), last), 0).rgb;
    vec3  f    = texelFetch(src, clamp(ip + ivec2(1, 0), ivec2(0), last), 0).rgb;
    vec3  h    = texelFetch(src, clamp(ip + ivec2(0, 1), ivec2(0), last), 0).rgb;

    vec3 mn4 = min(min(b, min(d, f)), h);
    vec3 mx4 = max(max(b, max(d, f)), h);

    // How far the lobe may go before the darkest channel reaches 0 or the brightest 1: high
    // precision reciprocals.
    vec3  hitMin = min(mn4, e) * (1.0 / (4.0 * mx4));
    vec3  hitMax = (1.0 - max(mx4, e)) * (1.0 / (4.0 * mn4 - 4.0));
    vec3  lobes  = max(-hitMin, hitMax);
    float lobe   = max(-FSR_RCAS_LIMIT, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0)) * sharpness;

    // The resolve needs the medium-precision reciprocal, or the tone shifts visibly.
    float rcpL = fsrRcpMed(4.0 * lobe + 1.0);
    return (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
}
