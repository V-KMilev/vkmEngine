/*
 * A cubic B-spline filter from four bilinear reads (Sigg and Hadwiger, GPU Gems 2 ch. 20). Bilinear
 * is smooth only within a texel, so anything sharp drawn from a magnified texture shows every
 * texel's square; the B-spline is smooth across them, and has none to show.
 */

// Where to read a texture @p size texels across, at @p uv, for the B-spline there: the two read
// points along each axis (at0, at1) and the weights of what they read (g0, g1).
void bsplineReads(vec2 uv, vec2 size, out vec2 at0, out vec2 at1, out vec2 g0, out vec2 g1) {
    vec2 st   = uv * size - 0.5;
    vec2 cell = floor(st);
    vec2 f    = st - cell;
    vec2 w0   = (1.0 / 6.0) * (1.0 - f) * (1.0 - f) * (1.0 - f);
    vec2 w1   = (1.0 / 6.0) * (4.0 - 6.0 * f * f + 3.0 * f * f * f);
    vec2 w2   = (1.0 / 6.0) * (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f);
    vec2 w3   = (1.0 / 6.0) * f * f * f;
    g0  = w0 + w1;
    g1  = w2 + w3;
    at0 = (cell - 0.5 + w1 / g0) / size;
    at1 = (cell + 1.5 + w3 / g1) / size;
}
