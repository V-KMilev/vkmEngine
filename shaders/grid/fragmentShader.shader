#version 330 core

in vec3 v_worldPos;
out vec4 FragColor;

uniform float u_gridScale;
uniform float u_gridFadeStart;
uniform float u_gridFadeEnd;

uniform vec3 u_cameraPos;

// Colors
const vec3 gridColorThin  = vec3(0.35);
const vec3 gridColorThick = vec3(0.45);
const vec3 axisColorX     = vec3(0.95, 0.25, 0.25);
const vec3 axisColorZ     = vec3(0.25, 0.5, 0.95);

vec4 grid(vec3 pos, float scale) {
    vec2 coord = pos.xz * scale;
    vec2 deriv = fwidth(coord);

    vec2 grid = abs(fract(coord - 0.5) - 0.5) / deriv;
    float line = min(grid.x, grid.y);

    vec4 color = vec4(gridColorThin, 1.0 - min(line, 1.0));

    // Major lines every 10 units
    vec2 coordMajor = pos.xz * scale * 0.1;
    vec2 derivMajor = fwidth(coordMajor);
    vec2 gridMajor = abs(fract(coordMajor - 0.5) - 0.5) / derivMajor;
    float lineMajor = min(gridMajor.x, gridMajor.y);

    float majorAlpha = 1.0 - min(lineMajor, 1.0);
    color.rgb = mix(color.rgb, gridColorThick, majorAlpha);
    color.a   = max(color.a, majorAlpha * 0.8);

    // Axis lines
    if (abs(pos.x) < deriv.x * 1.5)
        color = vec4(axisColorZ, 1.0);
    if (abs(pos.z) < deriv.y * 1.5)
        color = vec4(axisColorX, 1.0);

    return color;
}

void main() {
    vec4 color = grid(v_worldPos, 1.0 / u_gridScale);

    // Fade based on distance from camera (XZ only so looking up/down doesn't kill grid)
    float dist = length(v_worldPos.xz - u_cameraPos.xz);
    float fade = 1.0 - smoothstep(u_gridFadeStart, u_gridFadeEnd, dist);
    color.a *= fade;

    if (color.a < 0.01)
        discard;

    FragColor = color;
}