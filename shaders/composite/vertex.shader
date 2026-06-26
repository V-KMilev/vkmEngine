// Attribute-less fullscreen triangle: positions + UVs synthesized from
// gl_VertexID (no vertex buffer). Matches Core::ScreenTriangle's 3-vertex draw.
out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
