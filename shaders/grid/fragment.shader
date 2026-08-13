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
float gridFactor(vec2 coord, float spacing) {
    vec2 g = abs(fract(coord / spacing - 0.5) - 0.5) / fwidth(coord / spacing);
    return 1.0 - min(min(g.x, g.y), 1.0);
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

    vec3  color = mix(vec3(0.30), vec3(0.55), step(0.5, major));
    float alpha = max(minor * 0.40, major * 0.75);

    // World axes: the X axis is the line worldZ == 0 (red), Z axis worldX == 0 (blue).
    float axisX = 1.0 - min(abs(c.y) / fwidth(c.y), 1.0);
    float axisZ = 1.0 - min(abs(c.x) / fwidth(c.x), 1.0);
    if (axisX > 0.0) { color = vec3(0.85, 0.30, 0.30); alpha = max(alpha, axisX); }
    if (axisZ > 0.0) { color = vec3(0.30, 0.45, 0.90); alpha = max(alpha, axisZ); }

    float dist = length(c - u_camPos.xz);
    alpha *= 1.0 - smoothstep(u_extent * 0.18, u_extent * 0.48, dist);

    if (alpha < 0.002) discard;
    FragColor = vec4(color, alpha);
}
