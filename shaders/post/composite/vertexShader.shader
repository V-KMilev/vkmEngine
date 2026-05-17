/**
 * Composite vertex shader - attribute-less fullscreen triangle.
 *
 * Drawn with glDrawArrays(GL_TRIANGLES, 0, 3) and an empty VAO; the three
 * positions and UVs are generated from gl_VertexID, so no vertex buffer is
 * needed. The triangle is oversized and clipped to the viewport.
 */
#version 420 core

out vec2 vUV;

void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
