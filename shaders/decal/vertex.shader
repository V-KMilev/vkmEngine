/**
 * Projected decal - box vertex stage.
 *
 * Draws the decal's unit cube ([-0.5, 0.5]) transformed by its world matrix. The
 * fragment stage does the real work: reconstructing the surface under each covered
 * pixel from depth and testing it against the box.
 */

layout (location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * u_model * vec4(aPos, 1.0);
}
