/*
 * Screen-space UI: viewport pixels (top-left origin) to clip space by an orthographic projection;
 * the rest passes through to the fragment stage.
 */
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aShape;
layout(location = 4) in vec4 aBorder;
layout(location = 5) in float aImage;

uniform mat4 u_proj;

out vec2 vUV;
out vec4 vColor;
out vec4 vShape;
out vec4 vBorder;
flat out float vImage;

void main() {
    vUV     = aUV;
    vColor  = aColor;
    vShape  = aShape;
    vBorder = aBorder;
    vImage  = aImage;
    gl_Position = u_proj * vec4(aPos, 0.0, 1.0);
}
