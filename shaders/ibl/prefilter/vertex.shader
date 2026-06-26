/**
 * IBL bake - shared cube vertex shader (prefiltered specular).
 */

layout (location = 0) in vec3 aPos;

uniform mat4 u_projection;
uniform mat4 u_view;

out vec3 vLocalPos;

void main() {
    vLocalPos = aPos;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
