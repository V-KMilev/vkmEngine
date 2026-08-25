/*
 * Anti-aliased world grid. Minor lines every 1 unit, major every 10, the world
 * X / Z axes coloured, and a distance fade so the far grid doesn't moire or
 * hard-edge at the quad rim. Alpha-blended over the resolved HDR scene.
 */

in vec3 vWorld;
out vec4 FragColor;

layout(binding = 19) uniform sampler2D u_sceneDepth;  // geometry target depth

uniform vec3  u_camPos;
uniform float u_extent;

// Line coverage at a given cell spacing: 1 on a line, 0 between, AA'd via the
// screen-space derivative so lines stay ~1px wide at any distance.
//
// The second term retires a level, and it starts only once a pixel spans a
// whole cell. Retiring at the point the lines stop resolving puts a second
// boundary inside the distance fade, which should own the grid's edge alone;
// the wash left by holding on this long is weighted low enough to read as haze.
float gridFactor(vec2 coord, float spacing) {
    vec2  uv = coord / spacing;
    vec2  w  = fwidth(uv);
    vec2  g  = abs(fract(uv - 0.5) - 0.5) / w;
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    return line * (1.0 - smoothstep(1.0, 1.75, max(w.x, w.y)));
}

void main() {
    // The grid draws into the post chain, whose targets carry no depth
    // attachment - so the LEQUAL occlusion test runs here instead: keep the
    // fragment only when nothing in the scene is in front of it.
    float sceneDepth = texelFetch(u_sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (gl_FragCoord.z > sceneDepth) discard;

    vec2 c = vWorld.xz;

    float minor = gridFactor(c,  1.0);
    float major = gridFactor(c, 10.0);

    vec3  color = mix(vec3(0.25), vec3(0.50), step(0.5, major));
    float alpha = max(minor * 0.25, major * 0.5);

    // World axes: the X axis is the line worldZ == 0 (red), Z axis worldX == 0 (blue).
    float axisX = 1.0 - min(abs(c.y) / fwidth(c.y), 1.0);
    float axisZ = 1.0 - min(abs(c.x) / fwidth(c.x), 1.0);
    if (axisX > 0.0) { color = vec3(0.85, 0.30, 0.30); alpha = max(alpha, axisX); }
    if (axisZ > 0.0) { color = vec3(0.30, 0.45, 0.90); alpha = max(alpha, axisZ); }

    // Horizontal distance only, so the fade is a disc on the ground rather than
    // a sphere around the eye - what is directly below the camera stays at full
    // strength however high it climbs. The fade reaches the quad's own edge, so
    // it is the last of the grid rather than a margin before it.
    float dist = length(c - u_camPos.xz);
    alpha *= 1.0 - smoothstep(u_extent * 0.7, u_extent, dist);

    if (alpha < 0.001) discard;
    FragColor = vec4(color, alpha);
}
