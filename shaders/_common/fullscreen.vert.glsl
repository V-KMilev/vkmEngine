/*
 * Attribute-less fullscreen triangle - the shared vertex stage of every
 * fullscreen pass (post chain, GTAO, contact shadows, bloom, composite,
 * BRDF LUT bake).
 *
 * gl_VertexID 0/1/2 expands to one triangle covering the screen twice over,
 * so a plain glDrawArrays(GL_TRIANGLES, 0, 3) with an empty VAO fills the
 * viewport with no vertex buffer. vUV spans 0..1 across the visible region.
 */

out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
