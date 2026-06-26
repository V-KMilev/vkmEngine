/**
 * IBL bake - shared cube vertex shader (equirect -> cubemap).
 *
 * Drawn with the unit-cube mesh; only position (location 0) is used. Emits
 * the local position so the fragment shader can treat it as a direction.
 */

layout (location = 0) in vec3 aPos;

uniform mat4 u_projection;
uniform mat4 u_view;

out vec3 vLocalPos;

void main() {
    vLocalPos = aPos;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
