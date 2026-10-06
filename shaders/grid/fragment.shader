/*
 * Anti-aliased world grid: minor lines every unit, major every 10, coloured X / Z axes, and a fade
 * so the far grid neither moires nor hard-edges at the quad rim. Blended over the resolved HDR scene.
 */
in vec3 vWorld;
out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // geometry target depth

#include "../camera.glsl"

uniform float u_extent;

// Line coverage at a cell spacing, AA'd by the screen derivative so lines stay ~1px wide. The
// second term retires a level only once a pixel spans a whole cell, leaving the edge to the fade.
float gridFactor(vec2 coord, float spacing) {
    vec2  uv   = coord / spacing;
    vec2  w    = fwidth(uv);
    vec2  g    = abs(fract(uv - 0.5) - 0.5) / w;
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    return line * (1.0 - smoothstep(1.0, 1.75, max(w.x, w.y)));
}

void main() {
    // The post chain's targets carry no depth attachment, so the LEQUAL test runs here.
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

    // Horizontal distance, so the fade is a disc on the ground however high the camera climbs;
    // it ends at the quad's edge.
    float dist = length(c - u_camera.cameraPosition.xz);
    alpha *= 1.0 - smoothstep(u_extent * 0.7, u_extent, dist);

    if (alpha < 0.001) discard;
    FragColor = vec4(color, alpha);
}
