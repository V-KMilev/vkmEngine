/*
 * A frame about a direction, for what is symmetric about it - a cosine lobe, a disk of taps, a
 * sample pattern - where any pair of vectors across it will do.
 */

// Two unit vectors across unit @p n, so (t, b, n) is a right-handed frame.
void basisAcross(vec3 n, out vec3 t, out vec3 b) {
    t = normalize(cross(abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n));
    b = cross(n, t);
}
