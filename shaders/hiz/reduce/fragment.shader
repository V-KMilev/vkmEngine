/**
 * Hi-Z reduction: the farthest depth over each 2x2 region of the source.
 *
 * One shader drives the whole pyramid. Level 0 reads the scene depth texture
 * and every later level reads the previous one; both are sampler2D and both
 * answer in .r, so the only thing that changes is which texture is bound and
 * which LOD is read.
 *
 * MAXIMUM, not minimum. Each source texel already holds the nearest surface at
 * that pixel; the reduction asks "how far away is the nearest surface, at
 * worst, anywhere in this region". A candidate further than that is behind
 * something everywhere it covers.
 *
 * The odd case is the one that bites. Halving an odd dimension leaves a row or
 * column out of every 2x2 group, and a maximum that skipped a texel is too
 * small - it claims the occluders are nearer than they are and culls geometry
 * that is actually visible. So an odd source pulls in the extra row/column
 * rather than assuming the region is exactly 2x2.
 */

in vec2 vUV;

out float FragColor;

uniform sampler2D u_src;
uniform float u_srcLod;
uniform ivec2 u_srcSize;    ///< Dimensions of the level being reduced.

float fetch(ivec2 texel) {
    return texelFetch(u_src, clamp(texel, ivec2(0), u_srcSize - 1), int(u_srcLod)).r;
}

void main() {
    const ivec2 base = ivec2(gl_FragCoord.xy) * 2;

    float far = fetch(base);
    far = max(far, fetch(base + ivec2(1, 0)));
    far = max(far, fetch(base + ivec2(0, 1)));
    far = max(far, fetch(base + ivec2(1, 1)));

    // An odd source leaves one row / column outside every 2x2 group; the last
    // output texel has to absorb it or the maximum is a lie.
    const bool oddX = (u_srcSize.x & 1) != 0;
    const bool oddY = (u_srcSize.y & 1) != 0;

    if (oddX) {
        far = max(far, fetch(base + ivec2(2, 0)));
        far = max(far, fetch(base + ivec2(2, 1)));
    }
    if (oddY) {
        far = max(far, fetch(base + ivec2(0, 2)));
        far = max(far, fetch(base + ivec2(1, 2)));
    }
    if (oddX && oddY) far = max(far, fetch(base + ivec2(2, 2)));

    FragColor = far;
}
