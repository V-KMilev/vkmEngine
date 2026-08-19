// Octahedral normal codec - pack/unpack a unit vector to [0,1]^2 with good
// precision. The depth prepass encodes the VIEW-space normal into the G-buffer;
// the post passes that read it back decode (composite, gtao, decals). The engine's
// GLSL preprocessor inlines this file; #include it after the #version line.

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 p = n.xy;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * signNotZero(p);
    return p * 0.5 + 0.5;
}

// Inverse of octEncode.
vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return normalize(n);
}
