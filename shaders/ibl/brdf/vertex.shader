/**
 * IBL bake - BRDF/DFG LUT, attribute-less fullscreen triangle.
 *
 * Drawn with glDrawArrays(GL_TRIANGLES, 0, 3) and an empty VAO.
 */

out vec2 vUV;

void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
