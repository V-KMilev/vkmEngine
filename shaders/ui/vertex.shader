/*
 * Screen-space UI: positions arrive in viewport pixels (top-left origin) and are
 * mapped straight to clip space by an orthographic projection. Colour and uv
 * pass through to the fragment stage.
 */

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 u_proj;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV    = aUV;
    vColor = aColor;
    gl_Position = u_proj * vec4(aPos, 0.0, 1.0);
}
